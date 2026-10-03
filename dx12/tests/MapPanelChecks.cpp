#include "Dx12Screen.h"
#include <cstdio>
#include <cstdlib>
#include "MinHook.h"
static FILE* logFile=stdout;
#include "MapPanel.h"
static uintptr_t Forward(void* a,void* b){return reinterpret_cast<uintptr_t>(a)+reinterpret_cast<uintptr_t>(b);}
static void Check(bool v){if(!v){puts("Map panel check failed");exit(1);}}
struct Dx12ScreenChecks {
 static void State(){Dx12Screen s;s.pending=true;s.panelAnchored=true;s.SetMapPanel(true);Check(s.pending&&s.mapPanel&&s.panelAnchored);s.SetMapPanel(false);Check(s.pending&&!s.mapPanel&&!s.panelAnchored);}
};
int main(){
 MapPanel::realShowTabs=MapPanel::realHideTabs=MapPanel::realShow3d=MapPanel::realHide3d=MapPanel::realShow2d=MapPanel::realHide2d=&Forward;
 Check(MapPanel::ShowTabs(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2))==3);
 Check(MapPanel::visible==4);
 MapPanel::Show3d(nullptr,nullptr);Check(MapPanel::visible==5);
 MapPanel::Hide3d(nullptr,nullptr);Check(MapPanel::visible==4); // tab switch keeps anchor
 MapPanel::Show2d(nullptr,nullptr);MapPanel::Hide2d(nullptr,nullptr);Check(MapPanel::visible==4);
 MapPanel::HideTabs(nullptr,nullptr);Check(MapPanel::visible==0);
 MapPanel::Show3d(nullptr,nullptr);MapPanel::ShowTabs(nullptr,nullptr);
 MapPanel::HideTabs(nullptr,nullptr);Check(MapPanel::visible==1);MapPanel::Hide3d(nullptr,nullptr);Check(MapPanel::visible==0);
 XrView v[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
 for(auto& eye:v){eye.pose.orientation.w=1;eye.pose.position={1,1.7f,3};}
 auto p=MapPanelPose(v);Check(fabsf(p.position.x-1)<.0001f&&fabsf(p.position.y-1.7f)<.0001f&&fabsf(p.position.z-1)<.0001f);
 for(auto& eye:v){eye.pose.orientation.y=sqrtf(.5f);eye.pose.orientation.w=sqrtf(.5f);}
 p=MapPanelPose(v);Check(fabsf(p.position.x+1)<.0001f&&fabsf(p.position.z-3)<.0001f);
 // Panel is level even if the user looks up when opening it.
 for(auto& eye:v){eye.pose.orientation={sinf(.3f),0,0,cosf(.3f)};}
 p=MapPanelPose(v);Check(p.orientation.x==0&&p.orientation.z==0&&fabsf(p.position.y-1.7f)<.0001f);
 Dx12ScreenChecks::State();puts("Map panel checks passed: placement, yaw, level orientation, pending-frame preservation, exit resets anchor.");
}
