#include "AppendAttributes/iGameAppendAttributesFilter.h"

#include "iGameAttributeSet.h"
#include "iGameCellArray.h"
#include "iGameDrawObject.h"
#include "iGameFlatArray.h"
#include "iGameMacro.h"
#include "iGamePointSet.h"
#include "iGamePoints.h"
#include "iGameStructuredMesh.h"
#include "iGameSurfaceMesh.h"
#include "iGameUnstructuredMesh.h"
#include "iGameVolumeMesh.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

IGAME_NAMESPACE_BEGIN

AppendAttributes::AppendAttributes() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

void AppendAttributes::AddInput(DataObject::Pointer data) {
    int n = this->GetNumberOfInputs();
    if (n == 1 && this->GetInput(0) == nullptr) {
        this->SetInput(0, data);
    } else {
        this->SetNumberOfInputs(n + 1);
        this->SetInput(n, data);
    }
}

void AppendAttributes::SetAppendPointData(bool enable) {
    if (m_AppendPointData != enable) {
        m_AppendPointData = enable;
        this->Modified();
    }
}

void AppendAttributes::SetAppendCellData(bool enable) {
    if (m_AppendCellData != enable) {
        m_AppendCellData = enable;
        this->Modified();
    }
}

void AppendAttributes::CollectInputs(DataObject::Pointer obj, std::vector<DataObject::Pointer>& out) {
    if (!obj) { return; }

    switch (obj->GetDataObjectType()) {
        case IG_POINT_SET:
        case IG_SURFACE_MESH:
        case IG_VOLUME_MESH:
        case IG_UNSTRUCTURED_MESH:
        case IG_STRUCTURED_MESH:
            out.push_back(obj);
            return;
        default:
            break;
    }

    // DrawObject 包装：取出它真正渲染的数据对象
    if (obj->GetDataObjectType() == IG_DRAW_OBJECT) {
        auto drawObj = DynamicCast<DrawObject>(obj);
        if (drawObj) {
            auto renderable = drawObj->GetRenderableObject();
            if (renderable && renderable.GetPointer() != obj.GetPointer()) {
                CollectInputs(renderable, out);
                return;
            }
        }
    }

    // 复合数据 / 多块数据：展平所有子块
    if (obj->HasSubDataObject()) {
        for (auto it = obj->SubDataObjectIteratorBegin(); it != obj->SubDataObjectIteratorEnd(); ++it) {
            CollectInputs(it->second, out);
        }
    }
}

IGsize AppendAttributes::GetCellCount(DataObject::Pointer obj) {
    if (!obj) { return 0; }

    switch (obj->GetDataObjectType()) {
        case IG_POINT_SET:
            return 0;
        case IG_SURFACE_MESH: {
            auto mesh = DynamicCast<SurfaceMesh>(obj);
            return mesh ? mesh->GetNumberOfFaces() : IGsize(0);
        }
        case IG_VOLUME_MESH: {
            auto mesh = DynamicCast<VolumeMesh>(obj);
            return mesh ? mesh->GetNumberOfVolumes() : IGsize(0);
        }
        case IG_UNSTRUCTURED_MESH: {
            auto mesh = DynamicCast<UnstructuredMesh>(obj);
            return mesh ? mesh->GetNumberOfCells() : IGsize(0);
        }
        case IG_STRUCTURED_MESH: {
            auto mesh = DynamicCast<StructuredMesh>(obj);
            return mesh ? mesh->GetNumberOfCells() : IGsize(0);
        }
        default: {
            auto cells = obj->GetCellArray();
            return cells ? cells->GetNumberOfCells() : IGsize(0);
        }
    }
}

namespace {

/**
 * @brief 深拷贝一个 CellArray（单元连接数组）。
 *
 * 【踩坑记录】不能直接 `dst = CellArray::New(); dst->DeepCopy(src);`：
 * CellArray 构造函数会预置 m_Offsets = {0}，而 CellArray::DeepCopy 对 m_Offsets
 * 是追加式拷贝、不会先清空，结果偏移量数组多出一个开头的 0，所有单元的起始偏移
 * 与长度整体错位一格（第 0 个单元长度为 0，最后一个单元越界读取），下游按单元
 * 遍历时会读到错位索引 → 0xc0000005 / 0xc0000374 闪退。
 * 先 Reset() 再 DeepCopy 即可得到与源完全一致的连接数组。
 * （只有 CellArray 如此；Points / AttributeSet / FlatArray 的 DeepCopy 会先重建目标。）
 */
CellArray::Pointer CloneCellArray(CellArray::Pointer src) {
    if (src == nullptr) { return nullptr; }
    auto dst = CellArray::New();
    dst->Reset();
    if (!dst->DeepCopy(src)) { return nullptr; }
    return dst;
}

} // namespace

DataObject::Pointer AppendAttributes::CreateOutputGeometry(DataObject::Pointer src) {
    if (!src) { return nullptr; }

    const IGenum type = src->GetDataObjectType();
    DataObject::Pointer output = DataObject::CreateDataObject(type);
    if (!output) { return nullptr; }

    if (type == IG_UNSTRUCTURED_MESH) {
        auto inMesh = DynamicCast<UnstructuredMesh>(src);
        auto outMesh = DynamicCast<UnstructuredMesh>(output);
        if (!inMesh || !outMesh) { return nullptr; }

        auto inPoints = inMesh->GetPoints();
        if (inPoints) {
            auto newPoints = Points::New();
            newPoints->DeepCopy(inPoints);
            outMesh->SetPoints(newPoints);
        }
        auto inCells = inMesh->GetCells();
        auto inTypes = inMesh->GetCellTypes();
        if (inCells && inTypes) {
            auto newCells = CloneCellArray(inCells);
            auto newTypes = UnsignedIntArray::New();
            newTypes->DeepCopy(inTypes);
            outMesh->SetCells(newCells, newTypes);
        }
    } else if (type == IG_SURFACE_MESH) {
        auto inMesh = DynamicCast<SurfaceMesh>(src);
        auto outMesh = DynamicCast<SurfaceMesh>(output);
        if (!inMesh || !outMesh) { return nullptr; }

        auto inPoints = inMesh->GetPoints();
        if (inPoints) {
            auto newPoints = Points::New();
            newPoints->DeepCopy(inPoints);
            outMesh->SetPoints(newPoints);
        }
        auto inFaces = inMesh->GetFaces();
        if (inFaces) {
            auto newFaces = CloneCellArray(inFaces);
            if (newFaces) { outMesh->SetFaces(newFaces); }
        }
    } else if (type == IG_VOLUME_MESH) {
        auto inMesh = DynamicCast<VolumeMesh>(src);
        auto outMesh = DynamicCast<VolumeMesh>(output);
        if (!inMesh || !outMesh) { return nullptr; }

        auto inPoints = inMesh->GetPoints();
        if (inPoints) {
            auto newPoints = Points::New();
            newPoints->DeepCopy(inPoints);
            outMesh->SetPoints(newPoints);
        }
        auto inVolumes = inMesh->GetVolumes();
        if (inVolumes) {
            auto newVolumes = CloneCellArray(inVolumes);
            if (newVolumes) { outMesh->SetVolumes(newVolumes); }
        }
    } else if (type == IG_POINT_SET) {
        auto inMesh = DynamicCast<PointSet>(src);
        auto outMesh = DynamicCast<PointSet>(output);
        if (!inMesh || !outMesh) { return nullptr; }

        auto inPoints = inMesh->GetPoints();
        if (inPoints) {
            auto newPoints = Points::New();
            newPoints->DeepCopy(inPoints);
            outMesh->SetPoints(newPoints);
        }
    } else if (type == IG_STRUCTURED_MESH) {
        auto inMesh = DynamicCast<StructuredMesh>(src);
        auto outMesh = DynamicCast<StructuredMesh>(output);
        if (!inMesh || !outMesh) { return nullptr; }

        igIndex* dims = inMesh->GetDimensionSize();
        if (dims) {
            igIndex newDims[3] = {dims[0], dims[1], dims[2]};
            outMesh->SetDimensionSize(newDims);
        }
        // 【必须】不要调用 SetExtent()：它的实现是 std::copy(e, e + 6, this->size)，
        // 会把 6 个值写进只有 3 个元素的 size[]。extent 在整套代码里从未被赋值
        // （恒为 {0,0,0,0,0,0}），调用 SetExtent 会把输出的 size 清零成 {0,0,0}，
        // 而点/属性仍是完整的 N 个，后续按 size 建单元时越界读写 → 闪退。
        auto inPoints = inMesh->GetPoints();
        if (inPoints) {
            auto newPoints = Points::New();
            newPoints->DeepCopy(inPoints);
            outMesh->SetPoints(newPoints);
        }
        // 与 VTK 读入结构网格时一致：补齐单元连接关系
        outMesh->GenStructuredCellConnectivities();
    } else {
        igError("AppendAttributesFilter: unsupported data object type ({}).", type);
        return nullptr;
    }

    output->SetName(src->GetName());
    return output;
}

void AppendAttributes::MergeAttributes(const std::vector<DataObject::Pointer>& inputs,
                                             AttributeSet::Pointer outAttrSet) {
    if (!outAttrSet) { return; }

    // 同名 + 同类（点/单元归属）的数组只保留最先出现的那一份，与 vtkAppendAttributes 一致
    std::set<std::pair<IGenum, std::string>> existing;

    for (const auto& input: inputs) {
        if (!input) { continue; }
        auto srcSet = input->GetAttributeSet();
        if (!srcSet) { continue; }
        auto buffer = srcSet->GetAllAttributes();
        if (!buffer) { continue; }

        for (IGsize i = 0; i < buffer->GetNumberOfElements(); ++i) {
            auto& src = buffer->GetElement(i);
            if (src.IsNone() || !src.pointer) { continue; }
            if (src.attachmentType != IG_POINT && src.attachmentType != IG_CELL) { continue; }
            if (src.attachmentType == IG_POINT && !m_AppendPointData) { continue; }
            if (src.attachmentType == IG_CELL && !m_AppendCellData) { continue; }

            const std::pair<IGenum, std::string> key{src.attachmentType, src.pointer->GetName()};
            if (!existing.insert(key).second) { continue; }

            AttributeSet::Attribute copy;
            if (!copy.DeepCopy(src)) {
                IGAME_CORE_WARN("AppendAttributesFilter: failed to copy array '{}'.", src.pointer->GetName());
                continue;
            }
            outAttrSet->AddAttribute(copy.type, copy.attachmentType, copy.pointer, copy.dataRange);
        }
    }
}

bool AppendAttributes::Execute() {
    std::vector<DataObject::Pointer> inputs;
    const int inputCount = this->GetNumberOfInputs();
    for (int i = 0; i < inputCount; ++i) {
        auto input = this->GetInput(i);
        if (input) { CollectInputs(input, inputs); }
    }
    m_CollectedInputCount = static_cast<int>(inputs.size());

    IGAME_CORE_INFO("AppendAttributesFilter: Execute() start ({} valid input(s), pointData = {}, cellData = {})",
                    m_CollectedInputCount, m_AppendPointData, m_AppendCellData);

    if (inputs.empty()) {
        igError("AppendAttributesFilter: no valid input data object!");
        return false;
    }
    if (!m_AppendPointData && !m_AppendCellData) {
        igError("AppendAttributesFilter: both point data and cell data are disabled!");
        return false;
    }

    // ---- 校验所有输入的点数 / 单元数逐项对应（与 vtkAppendAttributes 的要求一致） ----
    auto firstPoints = inputs[0]->GetPoints();
    const IGsize numPoints = firstPoints ? firstPoints->GetNumberOfPoints() : IGsize(0);
    const IGsize numCells = GetCellCount(inputs[0]);

    for (size_t i = 1; i < inputs.size(); ++i) {
        auto otherPoints = inputs[i]->GetPoints();
        const IGsize otherNumPoints = otherPoints ? otherPoints->GetNumberOfPoints() : IGsize(0);
        const IGsize otherNumCells = GetCellCount(inputs[i]);
        if (otherNumPoints != numPoints || otherNumCells != numCells) {
            igError("AppendAttributesFilter: input #{} has {} points / {} cells while input #0 has {} points / {} "
                    "cells; they must match one-to-one.",
                    i, otherNumPoints, otherNumCells, numPoints, numCells);
            return false;
        }
        if (inputs[i]->GetDataObjectType() != inputs[0]->GetDataObjectType()) {
            IGAME_CORE_WARN("AppendAttributesFilter: input #{} has a different data object type ({}) than input #0 "
                            "({}); the geometry of input #0 will be used.",
                            i, inputs[i]->GetDataObjectType(), inputs[0]->GetDataObjectType());
        }
    }

    if (numPoints == 0 && numCells == 0) {
        igError("AppendAttributesFilter: the inputs contain neither points nor cells!");
        return false;
    }

    auto output = CreateOutputGeometry(inputs[0]);
    if (!output) {
        igError("AppendAttributesFilter: failed to create the output geometry!");
        return false;
    }

    auto outAttrSet = AttributeSet::New();
    MergeAttributes(inputs, outAttrSet);
    output->SetAttributeSet(outAttrSet);

    output->SetName(inputs[0]->GetName() + "_appended");
    output->Modified();

    if (outAttrSet->GetNumberOfAttributes() > 0) { outAttrSet->ForceReConvertToDrawableData(); }

    IGAME_CORE_INFO("AppendAttributesFilter: merged {} attribute(s) from {} input(s).",
                    outAttrSet->GetNumberOfAttributes(), m_CollectedInputCount);

    this->SetOutput(0, output);
    return true;
}

IGAME_NAMESPACE_END
