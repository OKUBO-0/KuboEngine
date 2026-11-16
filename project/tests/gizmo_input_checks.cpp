#include "imgui.h"
#include "ImGuizmo.h"
#include "ImGuizmoManager.h"
#include "engine/editor/EntityGizmoTransform.h"
#include "engine/editor/EntityHistory.h"
#include "engine/editor/EntityGroupTransform.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); }
struct Context {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    Context() {
        ImGui::CreateContext(); ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        auto& io=ImGui::GetIO(); io.DisplaySize={800,600}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
        unsigned char* pixels=nullptr; int width=0,height=0;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        Require(pixels && width>0 && height>0,"Test font atlas failed");
    }
    ~Context() { ImGui::DestroyContext(); ImGui::SetCurrentContext(previous); ImGuizmo::SetImGuiContext(previous); }
};
Matrix4x4 Identity() { Matrix4x4 value{}; for (int i=0;i<4;++i) value.m[i][i]=1; return value; }
}

void CheckGizmoInput() {
    using Gizmo=Engine::Editor::ImGuizmoManager;
    for (auto operation : {Gizmo::Operation::Translate,Gizmo::Operation::Scale,Gizmo::Operation::Rotate})
    for (bool orthographic : {false,true}) for (bool snapping : {false,true}) for (float direction : {-1.0f,1.0f}) {
        Context context;
        auto view=Identity(); view.m[3][2]=5;
        Matrix4x4 projection{};
        if (orthographic) { projection=Identity(); projection.m[0][0]=0.25f; projection.m[1][1]=1.0f/3; projection.m[2][2]=1.0f/99.9f; projection.m[3][2]=-0.1f/99.9f; }
        else { projection.m[0][0]=1.5f; projection.m[1][1]=2; projection.m[2][2]=100.0f/99.9f; projection.m[2][3]=1; projection.m[3][2]=-10.0f/99.9f; }
        Engine::Scene::EntityWorld authored;
        const auto id=authored.CreateEntity("Input-test entity");
        const bool scaling=operation==Gizmo::Operation::Scale;
        const bool rotating=operation==Gizmo::Operation::Rotate;
        const float originX=rotating ? 0.0f:0.3f;
        authored.Find(id)->transform.position={originX,0,0};
        auto matrix=Identity(); matrix.m[3][0]=originX;
        if (scaling) { authored.Find(id)->transform.scale={2,3,4}; matrix.m[0][0]=2; matrix.m[1][1]=3; matrix.m[2][2]=4; }
        Engine::Editor::EntityHistory history;
        Engine::Scene::EntityWorld grouped;const auto groupRoot=grouped.CreateEntity("Group root"),groupChild=grouped.CreateEntity("Selected child",groupRoot),groupOther=grouped.CreateEntity("Other root");
        grouped.Find(groupRoot)->transform.position={originX-1,0,0};grouped.Find(groupChild)->transform.position={0,1,0};grouped.Find(groupOther)->transform.position={originX+1,0,0};
        Engine::Editor::EntitySelection groupSelection;std::string groupError;Require(groupSelection.Assign(grouped,{groupRoot,groupChild,groupOther},groupOther,groupError),groupError.c_str());
        const auto groupSelected=groupSelection.Capture();const auto groupStart=grouped.Capture().dump();const auto pivotStart=matrix;
        Engine::Editor::EntityHistory groupHistory;std::array<Matrix4x4,3> groupPoses{};const std::array<Engine::Scene::EntityId,3> groupIds{groupRoot,groupChild,groupOther};
        for(size_t index=0;index<groupIds.size();++index)Require(grouped.GetWorldMatrix(groupIds[index],groupPoses[index]),"Group input original pose missing");
        const auto start=authored.Capture().dump();
        const float pixelsPerUnit=orthographic ? 100.0f : 120.0f;
        const float startX=400+originX*pixelsPerUnit;
        const float step=scaling ? 0.25f:rotating ? 15.0f:1.0f; const float snap[3]={step,step,step};
        bool sawChange=false,overHandle=false;
        ImVec2 processedPointer{};
        float expectedRotation=0;
        const auto frame=[&](float mouseX,bool down,float mouseY=300) {
            auto& io=ImGui::GetIO(); io.AddMousePosEvent(mouseX,mouseY); io.AddMouseButtonEvent(0,down);
            ImGui::NewFrame(); Gizmo::BeginFrame();
            processedPointer=io.MousePos;
            ImGui::SetNextWindowPos({0,0},ImGuiCond_Always); ImGui::SetNextWindowSize({800,600},ImGuiCond_Always);
            ImGui::Begin("Gizmo input fixture",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
            Gizmo::SetDrawlist(); Gizmo::SetRect(0,0,800,600); Gizmo::SetOrthographic(orthographic); Gizmo::Enable(true);
            const auto before=authored.Capture();
            const auto groupFrom=matrix;
            const bool changed=Gizmo::Manipulate(&view.m[0][0],&projection.m[0][0],&matrix.m[0][0],operation,Gizmo::Mode::World,snapping ? snap : nullptr);
            if (changed) {
                sawChange=true; Engine::Scene::TransformComponent result; std::string error;
                Require(Engine::Editor::GizmoWorldToLocal(matrix,Identity(),authored.Find(id)->transform,result,error),error.c_str());
                authored.Find(id)->transform=result;
                Require(history.Record(before,authored,id,id,error),error.c_str());
                Require(Engine::Editor::ApplyGroupTransform(grouped,groupSelection,groupFrom,matrix,static_cast<int>(operation),groupHistory,error),error.c_str());
            }
            const bool usingGizmo=Gizmo::IsUsing();
            overHandle=Gizmo::IsOver();
            ImGui::End(); ImGui::Render(); return usingGizmo;
        };
        frame(startX,false); frame(startX,false);
        if (rotating) {
            // Locate the actual ring rather than assuming backend handle size.
            // Use its outermost hover at 45 degrees, away from collapsed X/Y rings.
            float radius=0;
            const float startAngle=std::numbers::pi_v<float>/4;
            for (int testRadius=20;testRadius<=100;++testRadius) {
                frame(startX+testRadius*std::cos(startAngle),false,300+testRadius*std::sin(startAngle));
                if (overHandle) radius=float(testRadius);
            }
            Require(radius>0,"Actual rotation ring was not hit-testable");
            const auto angleFrame=[&](float angle,bool down) { return frame(startX+radius*std::cos(angle),down,300+radius*std::sin(angle)); };
            angleFrame(startAngle,false);
            Require(angleFrame(startAngle,true),"Rotation ring did not acquire test pointer");
            const float actualStart=std::atan2(processedPointer.y-300,processedPointer.x-startX);
            const float delta=direction*20.0f*std::numbers::pi_v<float>/180;
            angleFrame(startAngle+delta*0.5f,true); angleFrame(startAngle+delta,true);
            // ImGui floors mouse coordinates during NewFrame. Use the actual
            // input ray angles, not the ideal fractional event positions.
            expectedRotation=-(std::atan2(processedPointer.y-300,processedPointer.x-startX)-actualStart);
            if (snapping) {
                const float angleStep=15.0f*std::numbers::pi_v<float>/180;
                expectedRotation=std::round(expectedRotation/angleStep)*angleStep;
            }
            Require(!angleFrame(startAngle+delta,false),"Rotation ring did not release test pointer");
        } else {
            Require(frame(startX,true),"Center gizmo handle did not acquire test pointer");
            const float pixels=scaling ? 16.0f:0.6f*pixelsPerUnit;
            frame(startX+direction*pixels*0.5f,true);
            frame(startX+direction*pixels,true);
            Require(!frame(startX+direction*pixels,false),"Gizmo did not release test pointer");
        }
        Require(sawChange,"Actual gesture made no changes");
        if (scaling) {
            const float ratio=1+direction*(snapping ? 0.25f:0.16f);
            for (int axis=0;axis<3;++axis) Require(std::abs(matrix.m[axis][axis]-(axis+2)*ratio)<0.002f,"Actual scale/snap ratio differs from expected drag ratio");
            Require(std::abs(matrix.m[3][0]-0.3f)<0.002f,"Uniform scale moved entity origin");
        } else if (rotating) {
            const float angle=std::atan2(matrix.m[0][1],matrix.m[0][0]);
            const float expected=expectedRotation;
            if (std::abs(angle-expected)>=0.002f) throw std::runtime_error("Actual rotation/snap mismatch: angle="+std::to_string(angle)+" expected="+std::to_string(expected)+" ortho="+std::to_string(orthographic)+" snap="+std::to_string(snapping));
            Require(std::abs(matrix.m[0][2])<0.002f && std::abs(matrix.m[1][2])<0.002f && std::abs(matrix.m[3][0]-originX)<0.002f,"Screen rotation changed another axis/origin");
        } else {
            const float expected=0.3f+direction*(snapping ? 1.0f:0.6f);
            Require(std::abs(matrix.m[3][0]-expected)<0.002f,"Actual translation/snap result differs from expected drag delta");
        }
        Require(std::abs(matrix.m[3][1])<0.002f && std::abs(matrix.m[3][2])<0.002f,"Screen translation changed another axis");
        const auto groupDelta=pivotStart.Inverse()*matrix;
        for(size_t index=0;index<groupIds.size();++index){Matrix4x4 actual{};Require(grouped.GetWorldMatrix(groupIds[index],actual),"Group input pose missing");const auto desired=groupPoses[index]*groupDelta;
            for(int row=0;row<4;++row)for(int column=0;column<4;++column)Require(std::abs(actual.m[row][column]-desired.m[row][column])<0.002f,"Actual group gesture double-transformed child/changed shared-pivot delta");}
        const auto groupFinish=grouped.Capture().dump();groupHistory.Commit();Require(groupHistory.UndoCount()==1 && groupSelection.Capture()==groupSelected,"Actual group gesture not one full-selection command");
        Require(groupHistory.Undo(grouped,groupSelection,groupError) && grouped.Capture().dump()==groupStart && groupSelection.Capture()==groupSelected,"Actual group gesture Undo incomplete");
        Require(groupHistory.Redo(grouped,groupSelection,groupError) && grouped.Capture().dump()==groupFinish && groupSelection.Capture()==groupSelected,"Actual group gesture Redo incomplete");
        const auto end=authored.Capture().dump(); history.Commit(); std::string error; Engine::Scene::EntityId selection=id;
        Require(history.UndoCount()==1 && history.Undo(authored,selection,error) && authored.Capture().dump()==start,"Actual input gesture did not undo in one authored command");
        Require(history.Redo(authored,selection,error) && authored.Capture().dump()==end,"Actual input gesture redo mismatch");
    }
    std::cout << "PASS: actual ImGui event-driven ImGuizmo center translation/uniform scale/rotation ring, perspective/orthographic +/- drags, relative snap/no-snap, release and authored grouped Undo/Redo\n";
    std::cout << "PASS: actual ImGuizmo translation/rotation/scale deltas applied to selected forest roots, child deduplication, snap/no-snap and one full-selection group Undo/Redo (not physical input QA)\n";
}
