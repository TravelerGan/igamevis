# ResampleToImageFilter 使用说明

## 功能

**Resample To Image（重采样到图像网格）** 过滤器，是 VTK `vtkResampleToImage` 的移植实现。
它把输入网格（`PointSet` 及其子类，如 `UnstructuredMesh` / `StructuredMesh` / `SurfaceMesh` /
`VolumeMesh`）在指定的采样区域上建立一个规则的图像网格，并在每一个网格格点处对输入的**点属性**
进行探针插值（probe），从而把网格上的场重采样为图像上的点场。

输出是一个 `StructuredMesh`（等价 `vtkImageData`）：

- 原点 `origin` = 采样区域的 `(xmin, ymin, zmin)`；
- 间距 `spacing[i]` = `(dims[i] == 1) ? 0 : (bounds[2i+1] - bounds[2i]) / (dims[i] - 1)`；
- 维度 = `SamplingDimensions`（默认 `10 × 10 × 10`）；
- 点数据中包含：
  - 输入点属性数组的**插值结果**（同名数组、保持源存储类型与分量数，落在网格外的格点取默认值 0）；
  - `char` 数组 **`vtkValidPointMask`**：格点落在输入网格内为 1，否则为 0；
  - `unsigned char` 数组 **`vtkGhostType`**（点/单元两级）：无效点标记为隐藏点，任意角点为无效
    点的单元标记为隐藏单元——与 VTK `SetBlankPointsAndCells` 的 ghost 空白化行为一致。

### 支持的单元类型

`顶点（Vertex） / 线段（Line） / 三角形（Triangle） / 四边形（Quad） / 四面体（Tetra） /
六面体（Hexahedron）`。

一维与二维单元的定位按**完整三维几何**判定，不会把平面外的格点误判为「在单元内」：

| 单元 | 判定方式 |
| --- | --- |
| `Vertex` | 格点与顶点重合（按单元尺度取容差） |
| `Line` | 参数化投影求 `t`，再校验**点到直线的垂直距离** |
| `Triangle` | 重心坐标判定「面内」，再校验**点到三角形所在平面的距离** |
| `Quad` | 三维局部坐标 `(r, s)` 的最小二乘求解（Gauss–Newton），并校验**残差（点到曲面的距离）** 与 `\|r\|,\|s\| ≤ 1` |
| `Tetra` | 三维重心坐标 |
| `Hexahedron` | 三线性局部坐标 |

`棱柱 / 金字塔 / 多边形 / 多面体 / 二阶单元` 等**不受支持**的类型不会被静默跳过，见下文
「不支持的单元类型」与 `GetMessage()`。

## 参数

### 核心库参数

| 参数 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `SamplingDimensions` | `int[3]` | `{10, 10, 10}` | 图像各轴的格点数（大于 0） |
| `SamplingBounds` | `double[6]` | `{0,1, 0,1, 0,1}` | 显式采样区域，仅当 `UseInputBounds=false` 时生效 |
| `UseInputBounds` | `bool` | `true` | 使用输入数据包围盒作为采样区域（向内收缩 epsilon） |
| `FailOnUnsupportedCells` | `bool` | `true` | 输入含不受支持的单元类型时是否直接判定失败（不产出不完整结果） |
| `DisableInterpolationForDiscreteArrays` | `bool` | `true` | 对整型/字符型（离散）及 ID 类数组禁用线性插值，改用最近顶点取值 |

### Qt 参数面板

菜单 **算法处理 → 重采样到图像 (Resample To Image)** 打开停靠面板，包含：

- **采样维度**：X / Y / Z 三个格点数输入框；
- **使用输入数据包围盒**：取消勾选后可手填 X / Y / Z 的 `min ~ max` 采样范围（要求每个轴 `max > min`）；
- **遇到不支持的单元类型时终止执行**：对应 `FailOnUnsupportedCells`；
- **对 ID/整型等离散数组禁用线性插值**：对应 `DisableInterpolationForDiscreteArrays`；
- **执行前预估与诊断**：实时显示输入点数/单元数、各单元类型计数（区分受支持/不受支持）、
  预估输出格点数与点数据内存量级；点「执行」时若规模过大（> 256³ 格点或 > 256 MB）会再次弹窗确认，
  执行后在同一位置显示本次运行的诊断信息。

面板本身不直接写模型树：它发出 `resultReady(DataObject::Pointer)` 信号，由主窗口按
「算法结果」加入模型树并刷新渲染。

## 调用方式

```cpp
#include <Convert/iGameResampleToImageFilter.h>
#include <iGameStructuredMesh.h>

auto filter = iGame::ResampleToImageFilter::New();
filter->SetInput(input);                          // 输入：PointSet 子类
filter->SetSamplingDimensions(64, 64, 64);        // 可选，采样分辨率
// filter->SetSamplingBounds(x0, x1, y0, y1, z0, z1); // 可选，显式采样区域
// filter->SetUseInputBounds(true);               // 可选，默认 true
// filter->SetFailOnUnsupportedCells(true);       // 可选，默认 true
// filter->SetDisableInterpolationForDiscreteArrays(true); // 可选，默认 true

// 可选：执行前预估输出规模（不真正执行），用于界面提示大尺寸
IGsize gridPoints = 0, gridCells = 0;
double memoryMB = 0.0;
if (filter->EstimateOutputSize(gridPoints, gridCells, memoryMB)) {
    // gridPoints / gridCells / memoryMB
}

if (filter->Execute()) {
    auto out = iGame::DynamicCast<iGame::StructuredMesh>(filter->GetOutput());
    // out 即重采样后的图像（点场 + vtkValidPointMask + vtkGhostType）
} else {
    // 失败原因（含「不受支持的单元类型」明细）
    const std::string& why = filter->GetMessage();
}
// 成功时 GetMessage() 也包含本次运行的诊断摘要（规模、有效格点数、冲突/非插值数组清单）
```

辅助静态接口：

```cpp
iGame::ResampleToImageFilter::GetMaskArrayName();        // "vtkValidPointMask"
iGame::ResampleToImageFilter::GetSupportedCellTypesText(); // 支持类型的中文/英文清单
iGame::ResampleToImageFilter::IsCellTypeSupported(type);   // 单个单元类型是否受支持
```

## 属性处理策略

1. **保留数组名称与分量数**：输出数组与输入点属性同名、同维数。
2. **保留合理的原始存储类型**：输出数组按源数组的存储类型创建（`float`/`double`/`int`/
   `unsigned int`/`char`/`unsigned char`/`short`/`unsigned short`/`long long`/`unsigned long long`），
   不再一律强制为 `float`。
3. **ID 类数组禁止线性插值**：
   - 整型/字符型（离散）数组，以及名称以 `Id` / `Ids` 结尾（如 `PointIds`、`GlobalCellIds`、
     `vtkOriginalPointIds`）或属固定 ID 名单的数组，**不做线性插值**，改用「包含该格点的源单元中
     权重最大的顶点取值」（最近顶点采样）。对 ID 做线性插值会插出并不存在的 ID，语义错误。
   - 浮点数组（`float`/`double`）始终按 VTK 语义做线性插值。
   - 如需与 VTK 完全一致（对 ID 也插值），调用 `SetDisableInterpolationForDiscreteArrays(false)`。
4. **单元数据（cell data）语义**：与 `vtkProbeFilter` 一致——被快照为输出点数据，并保持源数组类型；
   与某点数组**同名**的单元数组被丢弃，即 **Point Data 优先**，同时 `GetMessage()` 会明确列出被忽略的
   同名单元数组，避免误以为单元数据也参与了输出。

## 不支持的单元类型

`Execute()` 会先扫描输入的全部单元类型：

- `FailOnUnsupportedCells = true`（默认）：一旦发现不受支持的单元，**直接返回 `false`**，
  不产出可能不完整的结果；`GetMessage()` 给出类型名与数量，例如
  `输入包含 1 个不受支持的单元（Prism×1）；本过滤器仅支持 Vertex / Line / Triangle / Quad / Tetra / Hexahedron。`
- `FailOnUnsupportedCells = false`：继续执行，但 `GetMessage()` 仍会给出同样的告警，并说明
  「这些单元覆盖的格点将保持无效（`vtkValidPointMask = 0`）」，不会静默跳过。

## 使用示例

示例程序：`Examples/Filter/Convert/TestResampleToImage.cpp`
测试模型：`Examples/Models/ResampleCube.vtk`（标量字段）、`Examples/Models/ResampleCubeVector.vtk`（三分量字段）

示例硬编码相对路径并在读入后自动执行重采样，输出各轴分辨率、原点、间距、有效点数量、首个有效格点的
插值场，并读取 `vtkGhostType` 抽出有效单元表面进行显示：

- `ResampleCube.vtk` / `ResampleCubeVector.vtk`：一个挖掉角部小立方体的四面体立方体（27 点 /
  42 个四面体），带点字段 `field`（分别为 1 分量 `x²+y²+z²` 与 3 分量 `(x, y, z)`），烘焙矩形区域外
  的 ghost 空白化效果。
- 运行方式（工作目录为 `build-msvc`）：`.\Release\testResampleToImage.exe`

## 注意事项

1. **输入类型**：只接受 `PointSet` 及其子类；空数据或非网格输入会报错并返回 `false`。
2. **支持的单元类型**：见上文表格；不受支持的单元默认直接失败并提示，不会被静默跳过。
3. **一维/二维单元**：Line/Triangle/Quad 均按三维几何判定平面外/轴线外的格点，因此用二维网格
   （如壳体、剖面、线框）重采样时，只有真正落在几何上的格点有效。
4. **插值缓冲区**：按输入属性的最大分量数动态分配，任意分量数（含 >16 分量）均无越界风险。
5. **性能**：点定位采用「单元包围盒预过滤 + 遍历」，`SamplingDimensions` 过大或输入单元数过多时
   耗时线性增长；可用 `EstimateOutputSize()` 在执行前估算规模并按需调低分辨率。
6. **空白化显示**：iGame 渲染默认不会自动隐藏 ghost 单元；如需与 ParaView 一致的「有效区域」显示，
   请用 `ModelGeometryFilter` 读取输出上的 `vtkGhostType` 数组（测试用例中已演示）。
7. **容差**：平面/轴线距离容差取「单元尺度（单元点集最大两两距离）× 1e-6」，参数域边界容差 1e-7。
   极端细长或退化单元可能因容差而被判为无效，属预期行为。