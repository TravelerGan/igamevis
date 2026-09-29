#ifndef iGameWarpByScalarFilter_h
#define iGameWarpByScalarFilter_h

#include "iGameFilter.h"

#include <string>

IGAME_NAMESPACE_BEGIN

/**
 * @brief 按标量沿法向移动点（任务 #7，对应 VTK vtkWarpScalar / ParaView「Warp By Scalar」）。
 *
 * 对每个点 i 计算位移
 * @code
 *     x_new[i] = x[i] + normal[i] * scalar[i] * ScaleFactor
 * @endcode
 * 其中 normal[i] 的取法与 VTK 一致：
 *   - XYPlane 打开时忽略法向，直接把 Z 分量设为 scalar * ScaleFactor（carpet plot 行为）；
 *   - UseNormal 打开时使用用户指定的 Normal；
 *   - 否则优先使用输入的点法向数组（IG_NORMAL / IG_POINT，默认名 "Normals"）；
 *   - 输入没有点法向时退回用户指定的 Normal，并给出警告。
 *
 * 输出是一个新的数据对象（拓扑、属性与输入一致，仅点坐标改变），不修改输入。
 */
class WarpByScalar : public Filter {
public:
    I_OBJECT(WarpByScalar);
    static Pointer New() { return new WarpByScalar; }

    /// 用于变形的点标量数组名（ParaView: Select Input Scalars）。为空时自动取第一个点属性。
    void SetScalarsArrayName(const std::string& name);
    const std::string& GetScalarsArrayName() const { return m_ScalarsArrayName; }

    /// 缩放系数（ParaView: Scale Factor）。
    void SetScaleFactor(double scale);
    double GetScaleFactor() const { return m_ScaleFactor; }

    /// 是否强制使用用户指定的法向（ParaView: Use Normal）。
    void SetUseNormal(bool use);
    bool GetUseNormal() const { return m_UseNormal; }

    /// 用户指定的法向（ParaView: Normal），仅在 UseNormal 打开或输入没有点法向时使用。
    void SetNormal(double nx, double ny, double nz);
    const double* GetNormal() const { return m_Normal; }

    /// XY 平面模式（ParaView: XY Plane）。
    void SetXYPlane(bool xyPlane);
    bool GetXYPlane() const { return m_XYPlane; }

    bool Execute() override;

protected:
    WarpByScalar();
    ~WarpByScalar() override = default;

private:
    std::string m_ScalarsArrayName{};
    double m_ScaleFactor{1.0};
    bool m_UseNormal{false};
    double m_Normal[3]{0.0, 0.0, 1.0};
    bool m_XYPlane{false};
};

IGAME_NAMESPACE_END
#endif
