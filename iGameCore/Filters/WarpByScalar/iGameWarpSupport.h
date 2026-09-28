#ifndef iGameWarpSupport_h
#define iGameWarpSupport_h

#include "iGameArrayObject.h"
#include "iGameAttributeSet.h"
#include "iGameCellArray.h"
#include "iGamePointSet.h"
#include "iGamePoints.h"
#include "iGameStructuredMesh.h"
#include "iGameSurfaceMesh.h"
#include "iGameUnstructuredMesh.h"
#include "iGameVolumeMesh.h"

#include <string>

IGAME_NAMESPACE_BEGIN

/**
 * @brief 「按标量变形 (Warp By Scalar)」与「按向量变形 (Warp By Vector)」的公共辅助函数。
 *
 * 变形只改写点坐标，不改变拓扑与属性。因此输出对象按输入类型复制一份，
 * 这样既不会污染管线中的原始数据，也不需要重建任何连接关系。
 * 行为对齐 VTK vtkWarpScalar / vtkWarpVector（ParaView 的 Warp By Scalar / Warp By Vector）。
 */
namespace WarpSupport {

/**
 * @brief 深拷贝一个 CellArray（单元连接数组）。
 *
 * 【踩坑记录】不能直接 `dst = CellArray::New(); dst->DeepCopy(src);`：
 *   - CellArray 的构造函数会预置一个偏移量 m_Offsets = {0}（iGameCellArray.cpp:277）；
 *   - 而 CellArray::DeepCopy 只重建 m_Buffer，对 m_Offsets 是 **追加式** 拷贝
 *     （`m_Offsets->DeepCopy(o->m_Offsets)`），并不会先清空；
 *   - 于是拷贝结果的偏移量数组变成 n+1 个元素（开头多一个 0），所有单元的起始
 *     偏移与长度整体错位一格：第 0 个单元长度为 0，第 k 个单元读到第 k-1 个单元
 *     的连接，最后一个单元越界读取。
 * 症状：只改点坐标时一切正常，但一旦下游按单元遍历（ModelGeometryFilter /
 * MeshTriangulationFilter / 渲染抽壳等）就会读到错位索引甚至越界，表现为
 * 0xc0000005(访问冲突) 或 0xc0000374(堆损坏) 闪退。
 *
 * 规避办法：先 Reset() 清掉构造函数预置的偏移量再 DeepCopy，结果与源完全一致。
 * （只有 CellArray 有这个问题；Points / AttributeSet / FlatArray 的 DeepCopy
 *   都会先重建目标数组，不受影响。）
 */
inline CellArray::Pointer CloneCellArray(CellArray::Pointer src) {
    if (src == nullptr) { return nullptr; }
    auto dst = CellArray::New();
    dst->Reset(); // 清掉构造函数预置的 m_Offsets = {0}
    if (!dst->DeepCopy(src)) { return nullptr; }
    return dst;
}

/**
 * @brief 把输入的所有属性深拷贝一份挂到输出上。
 *
 * 输出是管线里的一个新数据集，属性数组必须与输入互相独立，
 * 否则后续对任一方的修改会串到另一方（SurfaceMesh::DeepCopy 也是这么做的）。
 */
inline void CopyAttributes(DataObject::Pointer input, DataObject::Pointer output) {
    if (!input || !output) { return; }
    auto src = input->GetAttributeSet();
    if (src == nullptr) { return; }

    auto dst = AttributeSet::New();
    dst->DeepCopy(src);
    output->SetAttributeSet(dst);
}

/**
 * @brief 按输入数据对象的类型创建一份可以安全改写点坐标的输出副本。
 *
 * 支持的输入类型与 TransformFilter / PassArrays 保持一致：
 * IG_SURFACE_MESH / IG_VOLUME_MESH / IG_STRUCTURED_MESH /
 * IG_UNSTRUCTURED_MESH / IG_POINT_SET。
 *
 * @param dataObject 输入数据对象
 * @return 输出副本；类型不支持或复制失败时返回 nullptr
 */
inline PointSet::Pointer CreateOutputCopy(DataObject::Pointer dataObject) {
    if (!dataObject) { return nullptr; }

    switch (dataObject->GetDataObjectType()) {
        case IG_SURFACE_MESH: {
            auto input = DynamicCast<SurfaceMesh>(dataObject);
            if (!input) { return nullptr; }
            // 【必须】不要用 SurfaceMesh::DeepCopy：它内部用 CellArray::DeepCopy
            // 拷贝面数组，会把偏移量数组弄错位（见 CloneCellArray 的说明）。
            auto output = SurfaceMesh::New();
            output->SetName(input->GetName());
            auto newPoints = Points::New();
            if (!newPoints->DeepCopy(input->GetPoints())) { return nullptr; }
            output->SetPoints(newPoints);
            auto newFaces = CloneCellArray(input->GetFaces());
            if (newFaces) { output->SetFaces(newFaces); }
            CopyAttributes(dataObject, output);
            return output;
        }
        case IG_VOLUME_MESH: {
            auto input = DynamicCast<VolumeMesh>(dataObject);
            if (!input) { return nullptr; }
            auto output = VolumeMesh::New();

            // 单元（体）数组深拷贝：输出是管线里的新数据集，不能与输入共享同一份
            // 拓扑，否则之后任何编辑类操作（删单元 / GarbageCollection / Clip）
            // 会同时改到原模型。RandomVectorsFilter 也是这么做的。
            auto srcVolumes = input->GetVolumes();
            if (srcVolumes) {
                auto volumes = CloneCellArray(srcVolumes);
                if (volumes) { output->SetVolumes(volumes); }
            }
            output->SetName(input->GetName());
            auto newPoints = Points::New();
            if (!newPoints->DeepCopy(input->GetPoints())) { return nullptr; }
            output->SetPoints(newPoints);
            CopyAttributes(dataObject, output);
            return output;
        }
        case IG_STRUCTURED_MESH: {
            auto input = DynamicCast<StructuredMesh>(dataObject);
            if (!input) { return nullptr; }

            // 【必须】StructuredMesh::New() 返回的是裸指针（引用计数为 0），
            // 必须马上交给智能指针持有；否则只要有任何一次「按值传 SmartPointer」
            // 产生的临时对象析构，引用计数就会从 1 减到 0 并把对象 delete 掉，
            // 之后再用它就是 use-after-free（0xc0000005 / 0xc0000374 闪退）。
            StructuredMesh::Pointer output = StructuredMesh::New();

            // 【必须】只设置维度，绝不要调用 SetExtent()。
            // StructuredMesh::SetExtent 的实现是 std::copy(e, e + 6, this->size)，
            // 会把 6 个值写进只有 3 个元素的 size[]。而 extent 在整套代码里从来
            // 没有被赋值过（VTK 读入结构网格时只调用 SetDimensionSize），也就是恒为
            // {0,0,0,0,0,0}；一旦调用 SetExtent，输出的 size 就会被清零成 {0,0,0}，
            // 但点数组/属性仍然是完整的 N 个，于是后续 GenStructuredCellConnectivities /
            // ModelGeometryFilter 按 size 建单元时会越界读写，表现为
            // 0xc0000005(访问冲突) 或 0xc0000374(堆损坏) 的闪退。
            // 传入前再拷一份：SetDimensionSize 会就地改写传入数组（2D 时把 s[2] 置 1）。
            igIndex* srcDims = input->GetDimensionSize();
            if (srcDims != nullptr) {
                igIndex newDims[3] = {srcDims[0], srcDims[1], srcDims[2]};
                output->SetDimensionSize(newDims);
            }
            output->SetName(input->GetName());

            auto newPoints = Points::New();
            if (!newPoints->DeepCopy(input->GetPoints())) { return nullptr; }
            output->SetPoints(newPoints);

            // 与 VTK 读入结构网格时的做法一致：补齐单元连接关系，
            // 否则渲染/几何提取阶段会拿到没有单元的网格。
            output->GenStructuredCellConnectivities();

            CopyAttributes(dataObject, output);
            return output;
        }
        case IG_UNSTRUCTURED_MESH: {
            auto input = DynamicCast<UnstructuredMesh>(dataObject);
            if (!input) { return nullptr; }
            auto output = UnstructuredMesh::New();

            // 单元数组 + 单元类型数组深拷贝，理由同上（不与输入共享拓扑）。
            auto srcCells = input->GetCells();
            if (srcCells) {
                auto cells = CloneCellArray(srcCells);
                auto types = UnsignedIntArray::New();
                if (input->GetCellTypes() != nullptr) { types->DeepCopy(input->GetCellTypes()); }
                if (cells) { output->SetCells(cells, types); }
            }
            output->SetName(input->GetName());
            auto newPoints = Points::New();
            if (!newPoints->DeepCopy(input->GetPoints())) { return nullptr; }
            output->SetPoints(newPoints);
            CopyAttributes(dataObject, output);
            return output;
        }
        case IG_POINT_SET: {
            auto input = DynamicCast<PointSet>(dataObject);
            if (!input) { return nullptr; }
            auto output = PointSet::New();
            output->SetName(input->GetName());
            auto newPoints = Points::New();
            if (!newPoints->DeepCopy(input->GetPoints())) { return nullptr; }
            output->SetPoints(newPoints);
            CopyAttributes(dataObject, output);
            return output;
        }
        default:
            return nullptr;
    }
}

/**
 * @brief 在点属性里查找一个数组。
 *
 * @param attrSet           输入的属性集合（可以为 nullptr）
 * @param name              属性名。为空表示「取第一个满足条件的点属性」
 * @param typeFilter        属性类型过滤，IG_NONE 表示不限制
 * @param exactComponents   要求的分量数；(>0 时精确匹配，<=0 表示不限制)。
 *                          精确匹配是必须的：ArrayObject::GetElement 会按
 *                          数组自身的分量数写缓冲区，分量数不符会越界。
 * @return 命中的数组；未命中返回 nullptr
 */
inline ArrayObject::Pointer FindPointArray(AttributeSet* attrSet, const std::string& name, IGenum typeFilter,
                                           int exactComponents) {
    if (attrSet == nullptr) { return nullptr; }

    auto buffer = attrSet->GetAllAttributes();
    if (buffer == nullptr) { return nullptr; }

    ArrayObject::Pointer firstMatch = nullptr;
    for (IGsize i = 0; i < buffer->GetNumberOfElements(); ++i) {
        auto& attr = buffer->GetElement(i);
        if (attr.IsNone()) { continue; }
        if (attr.attachmentType != IG_POINT) { continue; }
        if (!attr.pointer) { continue; }
        if (typeFilter != IG_NONE && attr.type != typeFilter) { continue; }
        if (exactComponents > 0 && attr.pointer->GetDimension() != exactComponents) { continue; }

        if (name.empty()) {
            if (!firstMatch) { firstMatch = attr.pointer; }
            continue;
        }
        if (attr.pointer->GetName() == name) { return attr.pointer; }
    }
    return firstMatch;
}

} // namespace WarpSupport

IGAME_NAMESPACE_END
#endif
