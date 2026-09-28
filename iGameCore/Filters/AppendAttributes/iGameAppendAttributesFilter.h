#ifndef iGameAppendAttributesFilter_h
#define iGameAppendAttributesFilter_h

#include "iGameFilter.h"
#include "iGameDataObject.h"

#include <vector>

IGAME_NAMESPACE_BEGIN

/**
 * @brief 合并多个输入的属性数组（任务 #16，对应 VTK vtkAppendAttributes / ParaView「Append Attributes」）。
 *
 * 与 AppendReduceFilter（网格合并去重）不同，本过滤器不修改几何：
 * 它要求所有输入的点数（以及单元数）逐项对应，输出直接采用第一个输入的
 * 几何与数据类型，然后把每个输入里出现过的点属性 / 单元属性依次复制进来，
 * 同名同类（名称 + 点/单元归属）的数组只保留最先出现的那一份。
 *
 * 与 ParaView 一致，可以通过 SetAppendPointData / SetAppendCellData 选择要
 * 追加哪些关联（对应 vtkAppendAttributes 的 FieldAssociations）。
 */
class AppendAttributesFilter : public Filter {
public:
    I_OBJECT(AppendAttributesFilter);
    static Pointer New() { return new AppendAttributesFilter; }

    /// 追加一个输入（与 AppendReduceFilter 相同的多输入管理方式）。
    void AddInput(DataObject::Pointer data);

    /// 是否追加点属性（ParaView: Field Associations -> Point Data）。
    void SetAppendPointData(bool enable);
    bool GetAppendPointData() const { return m_AppendPointData; }

    /// 是否追加单元属性（ParaView: Field Associations -> Cell Data）。
    void SetAppendCellData(bool enable);
    bool GetAppendCellData() const { return m_AppendCellData; }

    /// 最近一次执行收集到的有效输入个数（供界面提示用）。
    int GetNumberOfCollectedInputs() const { return m_CollectedInputCount; }

    bool Execute() override;

protected:
    AppendAttributesFilter();
    ~AppendAttributesFilter() override = default;

private:
    /// 递归收集输入（展平复合数据对象 / DrawObject 包装）。
    void CollectInputs(DataObject::Pointer obj, std::vector<DataObject::Pointer>& out);

    /// 取数据对象的单元数；点集没有单元时返回 0。
    static IGsize GetCellCount(DataObject::Pointer obj);

    /// 按输入类型创建空的输出几何（点与单元均深拷贝自 src）。
    static DataObject::Pointer CreateOutputGeometry(DataObject::Pointer src);

    /// 合并属性：把每个输入里的点/单元属性依次复制进 outAttrSet。
    void MergeAttributes(const std::vector<DataObject::Pointer>& inputs, AttributeSet::Pointer outAttrSet);

    bool m_AppendPointData{true};
    bool m_AppendCellData{true};
    int m_CollectedInputCount{0};
};

IGAME_NAMESPACE_END
#endif
