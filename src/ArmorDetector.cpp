// ============================================================================
//  ArmorDetector.cpp  ——  灯条检测的全部实现
// ----------------------------------------------------------------------------
//  这个文件是整个项目的核心。它按"任务二要求的四步"来组织：
//
//    ① buildMask      ：颜色阈值 → 二值图（把灯条抠出来，其他都变黑）
//    ② findLightBars  ：找轮廓 → 最小外接矩形 → 筛出候选灯条
//    ③ isLightBar     ：由矩形的 4 个角点算出灯条的上下端点
//    ④ tryMakePlate   ：把两根灯条配对成一块装甲板
//
//  读代码的建议：
//    先只看函数名和注释，理解"这一步要干什么"，不要一上来就抠语法细节。
//    看懂流程之后，再回来看每一行是怎么写的。
// ============================================================================

#include "lightbar/ArmorDetector.h"

#include <algorithm>    // std::max / std::min / std::swap
#include <cmath>        // std::fabs / std::sqrt
#include <cstdio>       // std::snprintf
#include <limits>       // std::numeric_limits

namespace lb
{

// ============================================================================
//  构造函数
// ----------------------------------------------------------------------------
//  `: cfg_(config)` 是"初始化列表"，等价于在函数体里写 cfg_ = config;
//  但初始化列表效率更高（直接构造，而不是先默认构造再赋值）。
//  这是 C++ 的惯用写法，看到冒号别慌。
// ============================================================================
ArmorDetector::ArmorDetector(const Config& config)
    : cfg_(config)
{
}

// ============================================================================
//  第一步：颜色阈值 → 二值 mask
// ----------------------------------------------------------------------------
//  输入：BGR 彩色图（OpenCV 读进来的默认格式）
//  输出：单通道二值图，灯条位置是 255（白），其他都是 0（黑）
//
//  为什么要先转到 HSV 再阈值，而不是直接对 BGR 三个通道分别设阈值？
//    因为 BGR 的数值会同时受"颜色"和"亮度"影响：
//      暗处的红灯可能是 (120, 20, 20)，亮处可能是 (255, 200, 200)
//    同一个红色，BGR 数值差得非常远，没法用一个固定范围框住。
//    而 HSV 把"是什么颜色(H)"和"有多亮(V)"分开了，
//    所以只要卡住 H 和 S，不管灯条亮还是暗，都能稳定地取出来。
//    这正是"颜色阈值"比"灰度阈值"强的地方 —— 本视频整体很暗，
//    如果用灰度阈值，灯条和背景的灰度可能差不多，根本分不开。
// ============================================================================
cv::Mat ArmorDetector::buildMask(const cv::Mat& bgrImage) const
{
    // cvtColor：颜色空间转换。COLOR_BGR2HSV 是"BGR 转 HSV"的固定常量。
    // 注意 OpenCV 里是 BGR 顺序，不是 RGB。
    cv::Mat hsv;
    cv::cvtColor(bgrImage, hsv, cv::COLOR_BGR2HSV);

    // inRange：把"落在 [下限, 上限] 区间内"的像素设成 255，其余设成 0。
    // 它的三个参数依次是：输入图、每通道下限、每通道上限、输出图。
    // 所以 Scalar(h, s, v) 里三个数的顺序是 H、S、V。
    cv::Mat mask;
    if (cfg_.enemyColor == EnemyColor::Red)
    {
        // 红色跨过了 H=0 这个点，所以要取两段再合并。
        cv::Mat lowPart;    // H ∈ [0, hRedLowMax]
        cv::Mat highPart;   // H ∈ [hRedHighMin, 179]
        cv::inRange(hsv,
                    cv::Scalar(0, cfg_.sMin, cfg_.vMin),
                    cv::Scalar(cfg_.hRedLowMax, 255, 255),
                    lowPart);
        cv::inRange(hsv,
                    cv::Scalar(cfg_.hRedHighMin, cfg_.sMin, cfg_.vMin),
                    cv::Scalar(179, 255, 255),
                    highPart);

        // 按位或：两张二值图只要有一张是白，结果就是白。
        // 这就是"两个色相段合并"的意思。
        mask = lowPart | highPart;
    }
    else
    {
        // 蓝色只有一段，直接一次 inRange 就够。
        cv::inRange(hsv,
                    cv::Scalar(cfg_.hBlueMin, cfg_.sMin, cfg_.vMin),
                    cv::Scalar(cfg_.hBlueMax, 255, 255),
                    mask);
    }

    // 可选的膨胀：让灯条稍微变粗、把断成两截的灯条连起来。
    // 默认关闭（原因见 Config.h：本视频有些灯条只有 3 像素宽）。
    if (cfg_.useDilate)
    {
        // getStructuringElement 生成一个"核"（一个小矩阵），
        // 膨胀时用它扫过整张图。MORPH_RECT 表示用矩形核。
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(cfg_.dilateSize, cfg_.dilateSize));
        cv::dilate(mask, mask, kernel);
    }

    return mask;
}

// ============================================================================
//  第二步：在 mask 上找轮廓，并筛出候选灯条
// ----------------------------------------------------------------------------
//  轮廓（contour）就是"白色连通区域的外边界"。
//  mask 里每根灯条都是一个白色小块，findContours 会把每个白块变成一串边界点。
// ============================================================================
std::vector<LightBar> ArmorDetector::findLightBars(const cv::Mat& mask) const
{
    // contours 的类型是"二维 vector"：外面一层是"有很多个轮廓"，
    // 里面一层是"一个轮廓由很多个点组成"。
    std::vector<std::vector<cv::Point>> contours;

    // ⚠️ 重要陷阱：OpenCV 4.x 的 findContours 会修改传入的图像！
    // 传 mask.clone() 是给它一份副本，这样 mask_ 保持原样，
    // 后面 showMask 显示出来的才是真正的二值图，而不是被改坏的东西。
    // 这个坑不报错、只是结果莫名其妙，属于"不知道就永远查不出来"的那种。
    cv::findContours(mask.clone(),
                     contours,
                     cv::RETR_EXTERNAL,        // 只取最外层轮廓（灯条是实心块，不需要内孔）
                     cv::CHAIN_APPROX_SIMPLE); // 只保留拐点，直线上的中间点丢掉

    std::vector<LightBar> result;   // 合格的灯条放这里

    // 用 size_t 而不是 int：contours.size() 的类型是 size_t（无符号整数），
    // 用它来比较和遍历不会有"有符号/无符号"的编译警告。
    for (size_t i = 0; i < contours.size(); ++i)
    {
        // contourArea：轮廓围成的面积（像素²）。
        // 先按面积快速筛一遍，太小的是噪点，太大的是墙面/衣服之类。
        double area = cv::contourArea(contours[i]);
        if (area < cfg_.minArea || area > cfg_.maxArea)
        {
            continue;   // continue = 跳过这一次循环，直接看下一个轮廓
        }

        // minAreaRect：求"最小外接旋转矩形"。
        // 为什么不用 boundingRect（正着的矩形）？
        // 因为灯条可能是斜的，正矩形会把周围一大片背景也框进来，
        // 导致宽高比完全失真。旋转矩形能贴着灯条转，尺寸才是真的。
        cv::RotatedRect rect = cv::minAreaRect(contours[i]);

        // 用几何特征判断它是不是灯条，是的话填进 bar
        LightBar bar;
        if (isLightBar(rect, bar))
        {
            result.push_back(bar);   // push_back = 往 vector 末尾追加一个元素
        }
    }

    return result;
}

// ============================================================================
//  第三步：判断"这个旋转矩形"像不像灯条，并算出上下端点
// ----------------------------------------------------------------------------
//  RotatedRect 里有三个信息：center（中心）、size（宽高）、angle（角度）。
//  但 angle 的含义在不同 OpenCV 版本里变过（4.5 前后是 -90~0 还是 0~90 不一样），
//  这是初学者最容易踩的坑之一。
//
//  所以我们不碰 angle，直接用它给出的 4 个角点来算 —— 角点坐标是确定无疑的。
//  这样写出来的代码，换 OpenCV 版本也不会出错。
// ============================================================================
bool ArmorDetector::isLightBar(const cv::RotatedRect& rect, LightBar& bar) const
{
    // 取出旋转矩形的 4 个角点。points() 会按顺序把 4 个点填进数组。
    cv::Point2f p[4];
    rect.points(p);

    // 计算 4 条边的长度。矩形的对边相等，所以其实只有两个不同的值。
    float edge[4];
    for (int k = 0; k < 4; ++k)
    {
        cv::Point2f d = p[(k + 1) % 4] - p[k];   // % 4 让第 4 条边回到 p[0]
        edge[k] = std::sqrt(d.x * d.x + d.y * d.y);
    }

    // 长边、短边（对边相等，取前两条边就能确定）
    float longEdge  = std::max(edge[0], edge[1]);
    float shortEdge = std::min(edge[0], edge[1]);

    // 退化情况：短边接近 0（一条线或一个点）。直接判为不合格。
    if (shortEdge < 1.0f)
    {
        return false;
    }

    // ---- 条件 1：长宽比 ----
    // 灯条是细长的，长宽比通常在 3~6。用范围而不是固定值，是因为
    // 远处的灯条只有几个像素，比例会飘。
    float ratio = longEdge / shortEdge;
    if (ratio < cfg_.minRatio || ratio > cfg_.maxRatio)
    {
        return false;
    }

    // ---- 条件 2：绝对长度 ----
    // 长宽比只能说明"细长"，但一根 3x9 像素的小碎块也算细长，
    // 那往往是地面倒影或噪点。用长边下限把它排除掉，这是最有效的一刀。
    if (longEdge < cfg_.minLength || longEdge > cfg_.maxLength)
    {
        return false;
    }

    // ---- 条件 3：竖直程度 ----
    // 灯条在图上基本是竖直的。用长边方向的 |dx|/|dy| 衡量：
    //   完全竖直 → dx = 0 → 比值 0
    //   越歪比值越大；比值 > 0.8 表示歪超过约 38.7°
    int longStart = (edge[0] >= edge[1]) ? 0 : 1;   // 哪一条是长边
    cv::Point2f dir = p[(longStart + 1) % 4] - p[longStart];
    float tanTilt = std::fabs(dir.x) / (std::fabs(dir.y) + 1e-6f);
    if (tanTilt > cfg_.maxTanTilt)
    {
        return false;
    }

    // ---- 算端点 ----
    // 关键思路：灯条的"上端点"和"下端点"，其实就是两条**短边**的中点。
    //   （灯条是个细长矩形，两条短边分别在它头顶和脚底）
    // 短边是 edge[0] 对应的那条，还是 edge[1] 对应的那条？
    //   edge[0] 是 p0-p1，edge[1] 是 p1-p2。
    //   如果 edge[0] 是短边，两条短边就是 (p0,p1) 和 (p2,p3)；
    //   如果 edge[1] 是短边，两条短边就是 (p1,p2) 和 (p3,p0)。
    int s = (edge[0] <= edge[1]) ? 0 : 1;

    // 这两条短边的中点，就是灯条的两个端点。
    cv::Point2f mid1 = (p[s] + p[(s + 1) % 4]) * 0.5f;
    cv::Point2f mid2 = (p[(s + 2) % 4] + p[(s + 3) % 4]) * 0.5f;

    // 谁在上面？图像坐标系里 y 越小越靠上。
    if (mid1.y < mid2.y)
    {
        bar.top = mid1;
        bar.bottom = mid2;
    }
    else
    {
        bar.top = mid2;
        bar.bottom = mid1;
    }

    // 填好其余信息
    bar.length = longEdge;
    bar.width  = shortEdge;
    bar.tilt   = tanTilt;

    return true;
}

// ============================================================================
//  第四步：把两根灯条配成一块装甲板
// ----------------------------------------------------------------------------
//  这一步是"从一堆候选里挑出正确组合"，也是检测效果好坏的关键。
//  四道检查，任何一道不过就说明这两根不可能是同一块装甲板：
//
//    ① 两根灯条差不多高       （同一块板上的灯条是一样长的）
//    ② 两根灯条中心差不多高   （它们并排站着，不会一高一低）
//    ③ 水平间距在合理范围     （太近是同一根被拆成两半，太远是两块不同的板）
//    ④ 左边那根放 left，右边放 right（保证后面画图的顺序固定）
//
//  第②条就是用来排除"地面倒影"的：倒影在真实灯条的正下方，
//  两者中心的高度差大约是一整根灯条的高度，远超 0.5 倍的容忍度。
// ============================================================================
bool ArmorDetector::tryMakePlate(const LightBar& a, const LightBar& b,
                                 ArmorPlate& plate) const
{
    float heightA = a.length;
    float heightB = b.length;
    float averageHeight = (heightA + heightB) * 0.5f;

    if (averageHeight < 1e-3f)   // 防止除以 0
    {
        return false;
    }

    // ① 高度差不能太大
    if (std::fabs(heightA - heightB) / averageHeight > cfg_.maxHeightDiffRatio)
    {
        return false;
    }

    // ② 中心高度差不能太大（排除倒影的关键）
    if (std::fabs(a.center().y - b.center().y) > cfg_.maxCenterYDiffRatio * averageHeight)
    {
        return false;
    }

    // ③ 水平间距要合理。用"间距 ÷ 灯条高度"这个比值来判断，
    //    这样不管装甲板离得远还是近，标准都一样（近大远小是同步缩放的）。
    float gap = std::fabs(a.center().x - b.center().x);
    if (gap < cfg_.minGapRatio * averageHeight ||
        gap > cfg_.maxGapRatio * averageHeight)
    {
        return false;
    }

    // ④ 按 x 坐标决定左右。这样后面 corners() 的顺序才永远是
    //    左上 → 右上 → 右下 → 左下。
    if (a.center().x < b.center().x)
    {
        plate.left  = a;
        plate.right = b;
    }
    else
    {
        plate.left  = b;
        plate.right = a;
    }

    plate.found = true;
    return true;
}

// ============================================================================
//  打分：一对灯条有多"像"同一块装甲板
// ----------------------------------------------------------------------------
//  如果画面里有两块装甲板，就会有不止一对候选。这时要选"最像"的一对。
//  打分规则：分数越小越好，由两部分组成：
//
//    1. 中心高度差 ÷ 平均高度    —— 越平齐越好
//    2. |实际间距÷高度 - 2.5|    —— 越接近理想比例 2.5 越好
//
//  为什么要第 2 项？因为倒影那一对虽然也可能通过前面的检查，
//  但倒影更矮，所以它的"间距÷高度"会明显偏大，这一项就能把它压下去。
// ============================================================================
float ArmorDetector::pairScore(const LightBar& a, const LightBar& b) const
{
    float averageHeight = (a.length + b.length) * 0.5f;
    if (averageHeight < 1e-3f)
    {
        return std::numeric_limits<float>::max();   // 返回一个"无穷大"，保证不会被选上
    }

    float dy = std::fabs(a.center().y - b.center().y);
    float dx = std::fabs(a.center().x - b.center().x);

    float scoreY    = dy / averageHeight;
    float scoreGap  = std::fabs(dx / averageHeight - cfg_.idealGapRatio);

    return scoreY + scoreGap;
}

// ============================================================================
//  对外的主函数：检测一帧
// ----------------------------------------------------------------------------
//  流程就是把上面几步串起来，然后"在所有可能的配对里挑分数最高的"。
// ============================================================================
bool ArmorDetector::detect(const cv::Mat& bgrImage, ArmorPlate& plate)
{
    plate.found = false;    // 先把结果设成"没找到"，避免上一帧的结果残留

    if (bgrImage.empty())   // 空图（比如视频读到头了）直接返回
    {
        return false;
    }

    // ①② 得到 mask，再从 mask 里找出所有候选灯条（存进成员变量，调试要用）
    mask_ = buildMask(bgrImage);
    bars_ = findLightBars(mask_);

    // ③④ 两两配对，挑出分数最低的一对
    float bestScore = std::numeric_limits<float>::max();

    // 双重循环遍历所有"两根的组合"：
    // i 从 0 到 n-1，j 从 i+1 到 n-1，这样每对只算一次，也不会自己配自己。
    for (size_t i = 0; i < bars_.size(); ++i)
    {
        for (size_t j = i + 1; j < bars_.size(); ++j)
        {
            ArmorPlate candidate;
            if (!tryMakePlate(bars_[i], bars_[j], candidate))
            {
                continue;   // 这两根配不上，试下一对
            }

            float score = pairScore(bars_[i], bars_[j]);
            if (score < bestScore)
            {
                bestScore = score;
                plate = candidate;   // 记住当前最优解
            }
        }
    }

    return plate.found;
}

// ============================================================================
//  可视化：把检测结果画到图上
// ----------------------------------------------------------------------------
//  颜色是 BGR 顺序！cv::Scalar(蓝, 绿, 红)
//    (0, 255, 0)   → 绿
//    (0, 255, 255) → 黄
//    (0, 0, 255)   → 红
//    (255, 0, 0)   → 蓝
//  记住这个"反着写"的规则，能省掉很多"为什么我画出来是蓝色"的困惑。
// ============================================================================
void ArmorDetector::draw(cv::Mat& image, const ArmorPlate& plate) const
{
    // ---- 先画所有候选灯条 ----
    // 这样调参时你能一眼看到：滤出来的候选到底对不对、有没有漏、有没有多。
    // 语法说明：`const auto& bar` 是"范围 for 循环"，
    // 意思是"把 bars_ 里的每个元素依次取出来，叫它 bar"。
    // & 表示引用（不复制），const 表示不修改。
    for (const auto& bar : bars_)
    {
        cv::line(image,
                 toPoint(bar.top),      // 线段的起点
                 toPoint(bar.bottom),   // 终点
                 cv::Scalar(0, 255, 255),   // 黄色
                 1);                    // 线宽 1 像素

        // 端点画成小圆点：上端点红色、下端点蓝色。
        // 最后一个参数 -1 表示"实心圆"（正数表示只画圆的轮廓，数字是线宽）。
        cv::circle(image, toPoint(bar.top), 2, cv::Scalar(0, 0, 255), -1);
        cv::circle(image, toPoint(bar.bottom), 2, cv::Scalar(255, 0, 0), -1);
    }

    if (!plate.found)
    {
        return;   // 没找到装甲板，画到这就行了
    }

    // ---- 再画最终选中的装甲板轮廓 ----
    // 按 左上 → 右上 → 右下 → 左下 的顺序连成闭合四边形。
    std::vector<cv::Point> quad;
    std::vector<cv::Point2f> floatCorners = plate.corners();
    for (const auto& p : floatCorners)
    {
        quad.push_back(toPoint(p));
    }

    // polylines：画折线。第三个参数 true 表示"自动把终点连回起点"（闭合）。
    cv::polylines(image, quad, true, cv::Scalar(0, 255, 0), 2);

    // 中心点：一个绿色实心圆
    cv::circle(image, toPoint(plate.center()), 3, cv::Scalar(0, 255, 0), -1);

    // 文字：写 "armor" 和灯条高度，方便判断远近
    // putText 的参数：图、文字、左下角位置、字体、字号、颜色、线宽
    char text[64];
    std::snprintf(text, sizeof(text), "armor h=%.0f", plate.left.length);
    cv::putText(image, text,
                cv::Point(quad[0].x, quad[0].y - 8),   // 在左上角上方一点
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,                        // 字号
                cv::Scalar(0, 255, 0),
                1);
}

// ============================================================================
//  调试：单独显示最近一次的 mask
// ----------------------------------------------------------------------------
//  调参顺序永远是：先看 mask 对不对，再看轮廓/筛选对不对，最后看配对。
//  如果 mask 里灯条都不完整，后面怎么调都是白费劲。
// ============================================================================
void ArmorDetector::showMask() const
{
    if (mask_.empty())
    {
        return;
    }
    cv::imshow("mask", mask_);
}

}   // namespace lb