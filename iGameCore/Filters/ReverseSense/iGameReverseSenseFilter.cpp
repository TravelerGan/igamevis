#include "iGameReverseSenseFilter.h"

#include "iGameArrayObject.h"
#include "iGameAttributeSet.h"
#include "iGameCellArray.h"
#include "iGamePoints.h"
#include "iGameSurfaceMesh.h"

#include <vector>

IGAME_NAMESPACE_BEGIN

namespace {

// 按输入数组的底层类型创建输出数组，避免属性复制时被统一转成 float。
ArrayObject::Pointer CreateArrayOfSameType(ArrayObject* inArray) {
    switch (inArray->GetArrayType()) {
        case IG_DoubleArray: return DoubleArray::New();
        case IG_IntArray:
        case IG_INTARRAY: return IntArray::New();
        case IG_UnsignedIntArray: return UnsignedIntArray::New();
        case IG_CharArray: return CharArray::New();
        case IG_UnsignedCharArray: return UnsignedCharArray::New();
        case IG_ShortArray: return ShortArray::New();
        case IG_UnsignedShortArray: return UnsignedShortArray::New();
        case IG_LongLongArray: return LongLongArray::New();
        case IG_UnsignedLongLongArray: return UnsignedLongLongArray::New();
        case IG_FloatArray:
        default: return FloatArray::New();
    }
}

} // namespace

ReverseSenseFilter::ReverseSenseFilter() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

bool ReverseSenseFilter::Execute() {
    m_Message.clear();

    auto input = DynamicCast<SurfaceMesh>(GetInput(0));
    if (input == nullptr) {
        m_Message = "ReverseSenseFilter only supports SurfaceMesh input";
        return false;
    }
    const IGsize pointNum = input->GetNumberOfPoints();
    const IGsize faceNum = input->GetNumberOfFaces();
    if (pointNum == 0 || faceNum == 0) {
        m_Message = "Input surface mesh is empty";
        return false;
    }

    auto output = SurfaceMesh::New();
    output->SetName(input->GetName());

    // 点集：独立拷贝，几何本身保持不变。
    auto outPoints = Points::New();
    outPoints->DeepCopy(input->GetPoints());
    output->SetPoints(outPoints);

    // 面：逐面写入新 CellArray；ReverseCells 时反转顶点环序。
    // 边与面-邻接关系由面环序推导，因此反转后统一重建即可保持一致。
    auto inFaces = input->GetFaces();
    auto outFaces = CellArray::New();
    std::vector<igIndex> ids;
    for (IGsize faceId = 0; faceId < faceNum; ++faceId) {
        const igIndex* cell = nullptr;
        const int n = inFaces->GetCellIds(faceId, cell);
        if (n <= 0) { continue; }
        ids.resize(n);
        if (m_ReverseCells) {
            for (int i = 0; i < n; ++i) { ids[i] = cell[n - 1 - i]; }
        } else {
            for (int i = 0; i < n; ++i) { ids[i] = cell[i]; }
        }
        outFaces->AddCellIds(ids.data(), n);
        UpdateProgress(0.5 * static_cast<double>(faceId + 1) / faceNum);
    }
    output->SetFaces(outFaces);

    // 属性：全部按原类型搬运；ReverseNormals 时把 IG_NORMAL 逐分量取反。
    auto inAttrs = input->GetAttributeSet();
    auto outAttrs = AttributeSet::New();
    if (inAttrs != nullptr) {
        auto all = inAttrs->GetAllAttributes();
        if (all) {
            const IGsize attrNum = all->GetNumberOfElements();
            for (IGsize a = 0; a < attrNum; ++a) {
                auto& attr = all->GetElement(a);
                if (attr.isDeleted || attr.pointer.IsNull()) { continue; }

                auto inArray = attr.pointer;
                auto outArray = CreateArrayOfSameType(inArray);
                outArray->SetName(inArray->GetName());
                const int dim = inArray->GetDimension();
                outArray->SetDimension(dim);

                const IGsize tupleNum = inArray->GetNumberOfElements();
                outArray->Resize(tupleNum);
                const bool negate = m_ReverseNormals && attr.type == IG_NORMAL;
                std::vector<double> values(dim > 0 ? dim : 1);
                for (IGsize t = 0; t < tupleNum; ++t) {
                    inArray->GetElement(t, values);
                    if (negate) {
                        for (int d = 0; d < dim; ++d) { values[d] = -values[d]; }
                    }
                    outArray->SetElement(t, values.data());
                }

                const IGsize index =
                    outAttrs->AddAttribute(attr.type, attr.attachmentType, outArray);
                if (index != static_cast<IGsize>(-1)) {
                    outAttrs->GetAttribute(index).UpdateAllDataRange();
                }
            }
        }
    }
    output->SetAttributeSet(outAttrs);

    // 面环序已改变：重建边与点→面/边→面邻接，使下游（选择/连通性）拿到的面连接关系与新环序一致。
    output->BuildEdges();
    output->BuildFaceEdgeLinks();
    output->BuildFaceLinks();

    UpdateProgress(1.0);
    SetOutput(output);
    return true;
}

IGAME_NAMESPACE_END
