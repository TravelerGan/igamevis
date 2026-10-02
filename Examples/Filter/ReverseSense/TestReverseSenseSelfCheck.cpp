// 反转面朝向过滤器回归自检（无 GUI、无外部模型依赖）。
// 覆盖：面环序反转、点/单元法向取反、其它属性原类型透传、
//       开关组合、非 SurfaceMesh/空网格的失败路径、输出邻接关系一致性。
#include <ReverseSense/iGameReverseSenseFilter.h>

#include <iGameArrayObject.h>
#include <iGameAttributeSet.h>
#include <iGameCellArray.h>
#include <iGamePoints.h>
#include <iGameSurfaceMesh.h>
#include <iGameUnstructuredMesh.h>

#include <cstdio>
#include <vector>

using namespace iGame;

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  -> %s : %s\n", ok ? "PASS" : "FAIL", what);
}

FloatArray::Pointer FillAttr(const char* name, int dim, const std::vector<double>& values) {
    auto arr = FloatArray::New();
    arr->SetName(name);
    arr->SetDimension(dim);
    arr->Resize(values.size() / dim);
    for (size_t i = 0; i < values.size() / dim; ++i) {
        float* dst = arr->RawPointer(static_cast<IGsize>(i));
        for (int d = 0; d < dim; ++d) { dst[d] = static_cast<float>(values[i * dim + d]); }
    }
    return arr;
}

// 两张三角形拼成的四边形面片：面 0 = (0,1,2)，面 1 = (0,2,3)。
// 附带点法向、点标量、单元法向、单元标量，用于验证属性搬运与法向取反。
SurfaceMesh::Pointer MakeQuadSurface() {
    auto mesh = SurfaceMesh::New();
    auto pts = Points::New();
    pts->AddPoint(Point(0.f, 0.f, 0.f));
    pts->AddPoint(Point(1.f, 0.f, 0.f));
    pts->AddPoint(Point(1.f, 1.f, 0.f));
    pts->AddPoint(Point(0.f, 1.f, 0.f));
    mesh->SetPoints(pts);

    auto faces = CellArray::New();
    faces->AddCellId3(0, 1, 2);
    faces->AddCellId3(0, 2, 3);
    mesh->SetFaces(faces);

    auto attrs = AttributeSet::New();
    attrs->AddAttribute(IG_NORMAL, IG_POINT,
                        FillAttr("Normals", 3, {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}));
    attrs->AddAttribute(IG_SCALAR, IG_POINT,
                        FillAttr("pressure", 1, {1, 2, 3, 4}));
    attrs->AddAttribute(IG_NORMAL, IG_CELL,
                        FillAttr("CellNormals", 3, {0, 0, 1, 0, 0, 1}));
    attrs->AddAttribute(IG_SCALAR, IG_CELL,
                        FillAttr("cellid", 1, {10, 20}));
    mesh->SetAttributeSet(attrs);
    return mesh;
}

// 单个五边形面，验证任意顶点数的环序反转。
SurfaceMesh::Pointer MakePolygonSurface() {
    auto mesh = SurfaceMesh::New();
    auto pts = Points::New();
    for (int i = 0; i < 5; ++i) {
        pts->AddPoint(Point(static_cast<float>(i), 0.f, 0.f));
    }
    mesh->SetPoints(pts);
    igIndex ids[5] = {0, 1, 2, 3, 4};
    auto faces = CellArray::New();
    faces->AddCellIds(ids, 5);
    mesh->SetFaces(faces);
    return mesh;
}

void CheckFace(const SurfaceMesh::Pointer& mesh, IGsize faceId, const std::vector<igIndex>& expect,
               const char* what) {
    const igIndex* ids = nullptr;
    const int n = mesh->GetFaces()->GetCellIds(faceId, ids);
    bool ok = (n == static_cast<int>(expect.size()));
    for (int i = 0; ok && i < n; ++i) { ok = (ids[i] == expect[i]); }
    Expect(ok, what);
}

void TestReverseDefaults() {
    std::printf("[case] 默认：反转面环序 + 法向取反\n");
    auto src = MakeQuadSurface();

    auto filter = ReverseSenseFilter::New();
    filter->SetInput(src);
    Expect(filter->Execute(), "Execute returns true");

    auto out = DynamicCast<SurfaceMesh>(filter->GetOutput());
    Expect(out != nullptr, "output is a SurfaceMesh");
    if (!out) return;

    Expect(out->GetNumberOfPoints() == 4, "point count unchanged");
    Expect(out->GetNumberOfFaces() == 2, "face count unchanged");

    CheckFace(out, 0, {2, 1, 0}, "face 0 reversed -> (2,1,0)");
    CheckFace(out, 1, {3, 2, 0}, "face 1 reversed -> (3,2,0)");

    // 点坐标保持不变
    bool coordsOk = true;
    const Point expectPts[4] = {Point(0, 0, 0), Point(1, 0, 0), Point(1, 1, 0), Point(0, 1, 0)};
    for (int i = 0; i < 4; ++i) {
        const Point& p = out->GetPoint(i);
        for (int d = 0; d < 3; ++d) {
            if (p[d] != expectPts[i][d]) coordsOk = false;
        }
    }
    Expect(coordsOk, "point coordinates unchanged");

    auto outAttrs = out->GetAttributeSet();
    Expect(outAttrs != nullptr, "output has attribute set");
    if (!outAttrs) return;

    auto pn = DynamicCast<FloatArray>(outAttrs->GetAttribute("Normals").pointer);
    Expect(pn != nullptr, "point normals present");
    Expect(pn && pn->RawPointer(0)[2] == -1.f && pn->RawPointer(1)[2] == -1.f,
           "point normals negated (z: 1 -> -1)");

    auto cn = DynamicCast<FloatArray>(outAttrs->GetAttribute("CellNormals").pointer);
    Expect(cn != nullptr, "cell normals present");
    Expect(cn && cn->RawPointer(0)[2] == -1.f, "cell normals negated");

    auto ps = DynamicCast<FloatArray>(outAttrs->GetAttribute("pressure").pointer);
    Expect(ps != nullptr && ps->RawPointer(0)[0] == 1.f && ps->RawPointer(3)[0] == 4.f,
           "scalar point attribute copied unchanged");

    auto cs = DynamicCast<FloatArray>(outAttrs->GetAttribute("cellid").pointer);
    Expect(cs != nullptr && cs->RawPointer(0)[0] == 10.f && cs->RawPointer(1)[0] == 20.f,
           "scalar cell attribute copied unchanged");
}

void TestToggles() {
    std::printf("[case] 开关组合\n");
    auto src = MakeQuadSurface();

    {
        auto filter = ReverseSenseFilter::New();
        filter->SetInput(src);
        filter->SetReverseCells(false);
        filter->SetReverseNormals(false);
        Expect(filter->Execute(), "both off Execute returns true");
        auto out = DynamicCast<SurfaceMesh>(filter->GetOutput());
        if (out) {
            CheckFace(out, 0, {0, 1, 2}, "cells kept when ReverseCells off");
            CheckFace(out, 1, {0, 2, 3}, "cells kept when ReverseCells off (2)");
            auto pn = DynamicCast<FloatArray>(out->GetAttributeSet()->GetAttribute("Normals").pointer);
            Expect(pn && pn->RawPointer(0)[2] == 1.f, "normals kept when ReverseNormals off");
        }
    }
    {
        auto filter = ReverseSenseFilter::New();
        filter->SetInput(src);
        filter->SetReverseNormals(false);
        Expect(filter->Execute(), "cells-only Execute returns true");
        auto out = DynamicCast<SurfaceMesh>(filter->GetOutput());
        if (out) {
            CheckFace(out, 0, {2, 1, 0}, "cells reversed, cells-only mode");
            auto pn = DynamicCast<FloatArray>(out->GetAttributeSet()->GetAttribute("Normals").pointer);
            Expect(pn && pn->RawPointer(0)[2] == 1.f, "normals kept, cells-only mode");
        }
    }
    {
        auto filter = ReverseSenseFilter::New();
        filter->SetInput(src);
        filter->SetReverseCells(false);
        Expect(filter->Execute(), "normals-only Execute returns true");
        auto out = DynamicCast<SurfaceMesh>(filter->GetOutput());
        if (out) {
            CheckFace(out, 0, {0, 1, 2}, "cells kept, normals-only mode");
            auto pn = DynamicCast<FloatArray>(out->GetAttributeSet()->GetAttribute("Normals").pointer);
            Expect(pn && pn->RawPointer(0)[2] == -1.f, "normals negated, normals-only mode");
        }
    }
}

void TestPolygon() {
    std::printf("[case] 多边形面环序反转\n");
    auto src = MakePolygonSurface();
    auto filter = ReverseSenseFilter::New();
    filter->SetInput(src);
    Expect(filter->Execute(), "polygon Execute returns true");
    auto out = DynamicCast<SurfaceMesh>(filter->GetOutput());
    if (out) {
        CheckFace(out, 0, {4, 3, 2, 1, 0}, "5-gon fully reversed");
    }
}

void TestAdjacency() {
    std::printf("[case] 输出邻接关系与反转后环序一致\n");
    auto src = MakeQuadSurface();
    auto filter = ReverseSenseFilter::New();
    filter->SetInput(src);
    Expect(filter->Execute(), "Execute returns true");
    auto out = DynamicCast<SurfaceMesh>(filter->GetOutput());
    if (!out) return;

    // 点 0 与点 2 同时属于两个面；点 1 只属于一个面。
    Expect(out->GetNumberOfLinks(2, SurfaceMesh::P2F) == 2, "point 2 links 2 faces (P2F)");
    Expect(out->GetNumberOfLinks(1, SurfaceMesh::P2F) == 1, "point 1 links 1 face (P2F)");
    Expect(out->GetNumberOfEdges() == 5, "2 triangles share an edge -> 5 edges");

    // 边界边判定不应因反转而改变：四边形的 4 条外边界边仍是边界边。
    int boundaryEdges = 0;
    for (IGsize e = 0; e < out->GetNumberOfEdges(); ++e) {
        if (out->IsBoundaryEdge(e)) ++boundaryEdges;
    }
    Expect(boundaryEdges == 4, "4 boundary edges after reversal");
}

void TestFailures() {
    std::printf("[case] 失败路径\n");
    {
        auto filter = ReverseSenseFilter::New();
        Expect(!filter->Execute(), "null input fails");
        Expect(!filter->GetMessage().empty(), "message set for null input");
    }
    {
        auto um = UnstructuredMesh::New();
        um->GetPoints()->AddPoint(Point(0, 0, 0));
        um->GetPoints()->AddPoint(Point(1, 0, 0));
        um->GetPoints()->AddPoint(Point(0, 1, 0));
        igIndex ids[3] = {0, 1, 2};
        um->AddCell(ids, 3, IG_TRIANGLE);
        auto filter = ReverseSenseFilter::New();
        filter->SetInput(um);
        Expect(!filter->Execute(), "UnstructuredMesh input is rejected");
    }
    {
        auto empty = SurfaceMesh::New();
        empty->GetPoints()->AddPoint(Point(0, 0, 0));
        auto filter = ReverseSenseFilter::New();
        filter->SetInput(empty);
        Expect(!filter->Execute(), "surface with no faces fails");
    }
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestReverseDefaults();
    TestToggles();
    TestPolygon();
    TestAdjacency();
    TestFailures();

    std::printf("total checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
