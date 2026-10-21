// ============================================================================
//  main.cpp  ——  程序入口
// ----------------------------------------------------------------------------
//  这个文件只做"串流程"这一件事：
//    打开视频 → 一帧一帧读 → 交给检测器 → 画结果 → 输出 → 下一帧
//
//  所有算法细节都在 ArmorDetector.cpp 里，这里不出现任何"阈值""轮廓"。
//  这就是分层架构的好处：入口文件永远很简单，改算法不会牵动这里。
//
//  编译运行（在项目根目录）：
//     mkdir -p build && cd build
//     cmake ..
//     make -j
//     ./lightbar ../data/armor.avi
// ============================================================================

#include <opencv2/opencv.hpp>

#include <cstdlib>     // std::system
#include <iostream>    // std::cout / std::cerr
#include <string>      // std::string

#include "lightbar/ArmorDetector.h"
#include "lightbar/PoseSolver.h"

int main(int argc, char** argv)
{
    // ========================================================================
    //  第 1 步：读命令行参数
    // ------------------------------------------------------------------------
    //  argc 是"参数个数"，argv 是"参数字符串数组"。
    //  argv[0] 永远是程序自己的名字，真正的参数从 argv[1] 开始。
    //
    //  用法举例：
    //     ./lightbar                          → 用默认路径 data/armor.avi
    //     ./lightbar ../data/armor.avi        → 指定视频
    //     ./lightbar ../data/armor.avi --nogui → 不弹窗（WSL 没图形界面时用）
    // ========================================================================
    std::string videoPath = "data/armor.avi";
    bool showWindow = true;     // 是否弹出预览窗口
    bool saveImages = true;     // 是否每 10 帧存一张 jpg

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--nogui")
        {
            showWindow = false;
        }
        else if (arg == "--nosave")
        {
            saveImages = false;
        }
        else
        {
            videoPath = arg;    // 不是开关，那就当成视频路径
        }
    }

    // ========================================================================
    //  第 2 步：准备输出目录
    // ------------------------------------------------------------------------
    //  cv::imwrite / VideoWriter 如果目录不存在，不会报错，只是静默失败。
    //  这是很坑的一点，所以干脆先用系统命令建好目录。
    //  -p 的意思是"如果已存在就不要报错"。
    // ========================================================================
    if (std::system("mkdir -p output") != 0){
        std::cerr << "错误：无法创建output目录" << std::endl;
        return -1;
    }

    // ========================================================================
    //  第 3 步：打开视频
    // ========================================================================
    cv::VideoCapture capture(videoPath);

    // 一定要检查 isOpened()！路径错、缺解码库，它都不会抛异常，
    // 只是安安静静地读不到东西，然后你的程序"看起来在跑，其实啥也没干"。
    if (!capture.isOpened())
    {
        std::cerr << "打不开视频：" << videoPath << std::endl;
        std::cerr << "请检查：" << std::endl;
        std::cerr << "  1) 路径对不对（相对路径是相对于你运行程序时所在的目录）" << std::endl;
        std::cerr << "  2) 编译 OpenCV 时有没有装视频解码库（FFMPEG）" << std::endl;
        return -1;
    }

    // 读视频的基本信息。如果读不到（返回 0），就给个默认值，避免后面除以 0。
    double fps = capture.get(cv::CAP_PROP_FPS);
    if (fps <= 1.0)
    {
        fps = 25.0;
    }
    int frameWidth  = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    int frameHeight = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    int frameCount  = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));

    std::cout << "视频：" << videoPath << std::endl;
    std::cout << "分辨率：" << frameWidth << " x " << frameHeight
              << "，帧率：" << fps
              << "，总帧数：" << frameCount << std::endl;

    // ========================================================================
    //  第 4 步：准备输出视频
    // ------------------------------------------------------------------------
    //  VideoWriter 的四个参数：文件名、编码格式、帧率、画面尺寸。
    //  尺寸必须和写入的帧完全一致，否则写不进去（而且不报错，很坑）。
    //  fourcc 是"四字符编码"，'M','J','P','G' 表示用 MJPEG 编码。
    //  用 MJPG 是因为它几乎不需要额外的编码库，兼容性最好。
    // ========================================================================
    cv::VideoWriter writer;
    bool canWriteVideo = (frameWidth > 0 && frameHeight > 0);   // 分辨率读不到就别硬写
    if (canWriteVideo)
    {
        writer.open("output/result.avi",
                    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                    fps,
                    cv::Size(frameWidth, frameHeight));
        canWriteVideo = writer.isOpened();   // open 之后还要再确认一次真的开成功了
    }

    if (!canWriteVideo)
    {
        std::cerr << "警告：无法写入输出视频（分辨率读取失败或缺少编码器），"
                  << "结果只会存成逐帧 jpg。" << std::endl;
    }

    // ========================================================================
    //  第 5 步：创建检测器和位姿解算器
    // ------------------------------------------------------------------------
    //  对象在栈上创建，离开作用域（main 结束）会自动销毁，不需要手动 delete。
    // ========================================================================
    lb::Config config;                  // 全部用 Config.h 里的默认参数
    lb::ArmorDetector detector(config);
    
    if (showWindow)
    {
        // WINDOW_NORMAL 允许鼠标拖动改变窗口大小；默认的 WINDOW_AUTOSIZE 不能。
        cv::namedWindow("lightbar", cv::WINDOW_NORMAL);
    }

    // ========================================================================
    //  第 6 步：逐帧循环（整个程序的主体）
    // ========================================================================
    cv::Mat frame;                  // 用来装每一帧图像
    int frameIndex = 0;             // 当前是第几帧（从 0 开始）
    int detectedFrames = 0;         // 成功检出装甲板的帧数
    int poseFrames = 0;             // 成功解算出位姿的帧数

    // capture.read(frame) 每次填一帧进来；读到结尾会返回 false，循环自然结束。
    while (capture.read(frame))
    {
        // ---- 6.1 检测 ----
        lb::ArmorPlate plate;
        bool found = detector.detect(frame, plate);

        // ---- 6.2 画检测结果 ----
        detector.draw(frame, plate);

        if (found)
        {
            ++detectedFrames;
        }

        // ---- 6.4 写进输出视频 ----
        if (canWriteVideo)
        {
            writer.write(frame);
        }

        // ---- 6.5 每隔 10 帧存一张 jpg（方便挑图写报告）----
        if (saveImages && frameIndex % 10 == 0)
        {
            // std::to_string 可以把数字变成字符串，这样拼文件名很方便
            std::string filename = "output/frame_" + std::to_string(frameIndex) + ".jpg";
            cv::imwrite(filename, frame);
        }

        // ---- 6.6 显示当前帧 ----
        if (showWindow)
        {
            // try / catch 是"异常处理"：如果 imshow 因为环境不支持图形界面而失败，
            // 它会抛出一个异常，我们把异常接住，打印提示并关掉窗口模式，
            // 而不是让程序直接崩掉。
            try
            {
                cv::imshow("lightbar", frame);   // 顺便看一眼 mask
                detector.showMask();

                // waitKey 必须调用，否则窗口不会刷新。
                // 30 表示最多等 30 毫秒，大约等于 33 帧/秒。
                // 返回值是你按下的键的 ASCII 码；27 是 ESC。
                int key = cv::waitKey(30);
                if (key == 27)
                {
                    std::cout << "按下 ESC，提前结束。" << std::endl;
                    break;
                }
            }
            catch (const cv::Exception& e)
            {
                std::cerr << "无法显示窗口（可能没有图形界面）：" << e.what() << std::endl;
                std::cerr << "已自动切换为 --nogui 模式，结果仍然会存到 output/ 里。" << std::endl;
                showWindow = false;
            }
        }

        // ---- 6.7 每 30 帧打印一次进度 ----
        if (frameIndex % 30 == 0)
        {
            std::cout << "第 " << frameIndex << " 帧，累计检出 "
                      << detectedFrames << " 帧" << std::endl;
        }

        ++frameIndex;
    }

    // ========================================================================
    //  第 7 步：收尾
    // ------------------------------------------------------------------------
    //  release() 是主动释放资源。虽然对象析构时也会自动释放，
    //  但写出来更清楚，而且能保证视频文件被正确写完整。
    // ========================================================================
    capture.release();
    if (canWriteVideo)
    {
        writer.release();
    }
    if (showWindow)
    {
        cv::destroyAllWindows();
    }

    // ---- 统计信息 ----
    std::cout << "------------------------------" << std::endl;
    std::cout << "总帧数：" << frameIndex << std::endl;
    std::cout << "检出装甲板：" << detectedFrames << " 帧" << std::endl;
    if (frameIndex > 0)
    {
        double rate = 100.0 * detectedFrames / frameIndex;
        std::cout << "检出率：" << rate << " %" << std::endl;
    }
    std::cout << "成功解算位姿：" << poseFrames << " 帧" << std::endl;
    std::cout << "结果视频：output/result.avi" << std::endl;

    return 0;
}