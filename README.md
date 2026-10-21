# lightbar —— 装甲板灯条识别（培训阶段 2 · 任务二）

用 OpenCV 4.5.4 + CMake 写的灯条识别项目。逐帧读取视频，识别装甲板的两根灯条，
把四个端点按顺序连成矩形画出来；选做部分用 solvePnP 解算位姿并画 FLU 坐标轴。

## 一、编译

前提：已经按任务一编译安装好 OpenCV 4.5.4（默认在 `/usr/local/opencv4.5.4`）。

```bash
cd lightbar
mkdir -p build
cd build
cmake ..
make -j
```

`cmake ..` 的输出里应该能看到：

```
-- OpenCV 版本: 4.5.4
-- OpenCV 头文件: /usr/local/opencv4.5.4/include/opencv4
-- OpenCV 库: opencv_core;opencv_imgproc;...
```

## 二、运行

```bash
# 把视频放到 data/ 下，命令随便叫什么，这里用 armor.avi
cp /某处/装甲板视频.avi ../data/armor.avi

./lightbar ../data/armor.avi
```

常用参数：

| 参数 | 作用 |
| --- | --- |
| `--nogui` | 不弹窗，只存结果（WSL 没图形界面时用） |
| `--nosave` | 不存逐帧 jpg |

输出：

- `output/result.avi` —— 处理后的视频
- `output/frame_XXXX.jpg` —— 每 10 帧一张，方便挑图写报告

## 三、目录结构

```
lightbar/
├── CMakeLists.txt              # 构建脚本
├── README.md
├── data/                       # 放视频（自己创建，视频别提交到 git）
├── include/lightbar/
│   ├── Config.h                # 所有可调参数（调参只改这里）
│   ├── Types.h                 # LightBar / ArmorPlate 数据结构
│   ├── ArmorDetector.h         # 检测器类声明
│   └── PoseSolver.h            # 位姿解算类声明（选做）
└── src/
    ├── main.cpp                # 入口：读视频、循环、输出
    ├── ArmorDetector.cpp       # 核心：二值化、轮廓、筛选、配对、绘制
    └── PoseSolver.cpp          # 选做：solvePnP + 画坐标轴
```

## 四、参数速查（都在 `include/lightbar/Config.h`）

| 参数 | 默认 | 作用 | 调大 | 调小 |
| --- | --- | --- | --- | --- |
| `hRedLowMax` | 15 | 红色色相上界（低段） | 橙色/黄色混入 | 红色不全 |
| `sMin` | 90 | 饱和度下限 | 更干净，暗灯条漏 | 灰白干扰变多 |
| `vMin` | 60 | 亮度下限 | 排除倒影，远处漏 | 倒影/反光混入 |
| `minArea` | 15 | 轮廓面积下限 | 滤远处灯条 | 噪点变多 |
| `minRatio` | 2.5 | 长宽比下限 | 必须更细长 | 方形物混入 |
| `maxTanTilt` | 0.8 | 允许倾斜（tan） | 允许更歪 | 只收竖直的 |
| `maxCenterYDiffRatio` | 0.5 | 配对的高度差容忍 | 歪板能配上 | 排倒影更狠 |
| `idealGapRatio` | 2.5 | 理想间距/灯条高 | — | — |

本视频实测值（101 帧统计）：灯条长边 15~52 px、短边 3~10 px、长宽比 3.2~6.2、
两灯条间距 / 灯条高 = 2.26~2.71。

## 五、常见问题

1. **`Could not find a package configuration file provided by "OpenCV"`**
   说明 CMake 没找到 OpenCV。检查 `/usr/local/opencv4.5.4/lib/cmake/opencv4/OpenCVConfig.cmake`
   是否存在；不存在就是任务一没装完。存在就改 `CMakeLists.txt` 里的
   `OPENCV_INSTALL_PREFIX`。

2. **`fatal error: opencv2/opencv.hpp: No such file or directory`**
   `target_include_directories` 里没加 `${OpenCV_INCLUDE_DIRS}`。

3. **`undefined reference to cv::imshow(...)`**
   `target_link_libraries` 里没加 `${OpenCV_LIBS}`。

4. **`打不开视频`**
   路径错（相对路径是相对于你执行程序的目录），或者 OpenCV 编译时没开 FFMPEG。

5. **`无法显示窗口`**
   WSL 没有图形界面。用 `--nogui` 跑，然后看 `output/` 里的图和视频。

## 六、提交前检查

- [ ] 能 `cmake .. && make` 通过
- [ ] 跑完输出视频里每帧都有绿色框
- [ ] 代码里有注释，参数集中在 `Config.h`
- [ ] README 写清楚了怎么编译、怎么运行
- [ ] 没有把 `build/`、`output/`、视频文件提交上去（`.gitignore` 已处理）