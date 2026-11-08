#define XCX_MOD_UI_TESTING
#include "../runtime/ui/mods_page.cpp"
#define controller_keyboard_checks accepted_controller_keyboard_checks
#include "controller_keyboard_check.cpp"
#undef controller_keyboard_checks
static int mods_checks,mods_failures;
static void verify(bool yes,const char *name) {++mods_checks;if(!yes){++mods_failures;printf("FAIL Mods: %s\n",name);}}
static void mods_frame() {
    ImGui::NewFrame();layout_menu(1280,720,0.8f);ImGui::Render();
}
extern "C" int controller_keyboard_checks(void) {
    int prior=accepted_controller_keyboard_checks();
    ImGui::CreateContext();ImGuiIO &io=ImGui::GetIO();io.DisplaySize=ImVec2(1280,720);io.DeltaTime=1.0f/60;io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.Fonts->Build();
    mod_ui_reset("");
    ModView a={};strcpy(a.id,"alpha");strcpy(a.name,"Alpha Workshop");strcpy(a.version,"0.1.0");strcpy(a.title,"Alpha settings");a.status=MOD_LOADED;a.field_count=3;
    const char *ids[]={"hints","strength","theme"};for(int i=0;i<3;++i){ModField &f=a.fields[i];strcpy(f.id,ids[i]);strcpy(f.label,ids[i]);f.type=i+1;f.maximum=i==1?100:1;f.value=0;}
    a.fields[2].option_count=2;strcpy(a.fields[2].options[0].id,"calm");strcpy(a.fields[2].options[0].label,"Calm");strcpy(a.fields[2].options[1].id,"bright");strcpy(a.fields[2].options[1].label,"Bright");mod_ui_publish(&a);
    ModView b=a;strcpy(b.id,"beta");strcpy(b.name,"Beta Preferences");mod_ui_publish(&b);
    g_text.store(0);g_open.store(1);g_tab=TAB_MODS;g_tab_request=TAB_MODS;g_focus_page=TAB_MODS;
    mods_frame();mods_frame();verify(mods_widget_count==3,"three real settings widgets render");
    ModsWidget widget=mods_widgets[0];float x=(widget.low.x+widget.high.x)/2,y=(widget.low.y+widget.high.y)/2;
    io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(0,true);mods_frame();io.AddMouseButtonEvent(0,false);mods_frame();
    ModAction action={};verify(mod_ui_pop(&action)==1,"mouse activation queues action");verify(!strcmp(action.mod,"alpha")&&!strcmp(action.field,"hints")&&action.value==1,"action targets stable mod and field");
    // Drive real ImGui navigation while keeping the host/Lua thread out of rendering.
    ImGui::FocusWindow(widget.window);ImGui::SetFocusID(widget.id,widget.window);GImGui->NavInputSource=ImGuiInputSource_Keyboard;GImGui->NavCursorVisible=true;ImGui::ClearActiveID();
    io.AddKeyEvent(ImGuiKey_Space,true);mods_frame();io.AddKeyEvent(ImGuiKey_Space,false);mods_frame();
    verify(mod_ui_pop(&action)==1,"keyboard activates settings checkbox");
    io.AddKeyEvent(ImGuiKey_Tab,true);mods_frame();io.AddKeyEvent(ImGuiKey_Tab,false);mods_frame();
    verify(GImGui->NavId!=widget.id && GImGui->NavId!=0,"keyboard Tab moves to next setting");
    ImGui::SetFocusID(widget.id,widget.window);GImGui->NavInputSource=ImGuiInputSource_Gamepad;GImGui->NavCursorVisible=true;ImGui::ClearActiveID();
    io.ClearInputKeys();test_buttons=1ull<<B_A;pad_events(io,1);mods_frame();pad_events(io,0);mods_frame();
    verify(!mod_ui_pop(&action),"held opening controller input does not edit settings");
    test_buttons=0;pad_events(io,0);mods_frame();test_buttons=1ull<<B_A;pad_events(io,0);mods_frame();test_buttons=0;pad_events(io,0);mods_frame();
    verify(mod_ui_pop(&action)==1,"fresh mapped controller Confirm edits after release");
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown,true);mods_frame();io.AddKeyEvent(ImGuiKey_GamepadDpadDown,false);mods_frame();
    verify(GImGui->NavId!=widget.id && GImGui->NavId!=0,"controller moves focus between settings");
    // Only failed persistence offers retry; it queues the current value rather than an edit.
    a.persistence_dirty=1;mod_ui_publish(&a);mods_frame();verify(mods_retry_visible,"dirty page offers Retry save");
    float rx=(mods_retry_low.x+mods_retry_high.x)/2,ry=(mods_retry_low.y+mods_retry_high.y)/2;
    io.AddMousePosEvent(rx,ry);io.AddMouseButtonEvent(0,true);mods_frame();io.AddMouseButtonEvent(0,false);mods_frame();
    verify(mod_ui_pop(&action)==1 && !strcmp(action.mod,"alpha") && !strcmp(action.field,"hints") && action.value==a.fields[0].value,"Retry save queues same-value action for its owner");
    a.status=MOD_FAULTED;mod_ui_publish(&a);mods_frame();verify(mods_retry_visible,"faulted dirty page retains retry layout");
    io.AddMouseButtonEvent(0,true);mods_frame();io.AddMouseButtonEvent(0,false);mods_frame();verify(!mod_ui_pop(&action),"faulted page disables Retry save");
    a.status=MOD_LOADED;
    a.persistence_dirty=0;mod_ui_publish(&a);mods_frame();verify(!mods_retry_visible,"clean page has no Retry save control");
    ModSnapshot *snapshot=new ModSnapshot;mod_ui_snapshot(snapshot);action.generation=snapshot->generation;action.session=snapshot->session;action.type=MOD_BOOL;action.value=1;
    verify(mod_ui_submit(&action)==1,"queued action accepted while page active");
    menu_text_begin("Interruption","",10,0,0,0);mod_ui_snapshot(snapshot);verify(snapshot->pending==0,"text entry cancels pending Mods actions");verify(menu_text_active()!=0,"existing editor retains priority");
    g_text.store(0);g_open.store(1);g_tab_request=TAB_MODS;mods_frame();mods_frame();
    a.status=MOD_FAULTED;strcpy(a.diagnostic,"test fault");mod_ui_publish(&a);mods_frame();mod_ui_snapshot(snapshot);action.generation=snapshot->generation;action.session=snapshot->session;
    verify(mods_widget_count==3,"fault keeps focused layout present");verify(!mod_ui_submit(&action),"faulted settings reject actions");
    a.status=MOD_LOADED;mod_ui_publish(&a);mods_frame();mod_ui_snapshot(snapshot);action.generation=snapshot->generation;action.session=snapshot->session;
    verify(mod_ui_submit(&action)!=0,"renderer cleanup setup action");menu_vk_release();mod_ui_snapshot(snapshot);verify(snapshot->pending==0,"renderer release cancels pending actions");
    g_open.store(1);mod_ui_interaction(1);mod_ui_snapshot(snapshot);action.generation=snapshot->generation;action.session=snapshot->session;mod_ui_submit(&action);set_open(0);mod_ui_snapshot(snapshot);verify(snapshot->pending==0,"menu close cancels pending actions");
    mod_ui_shutdown();verify(!mod_ui_submit(&action),"teardown rejects stale actions");delete snapshot;
    ImGui::DestroyContext();printf("Mods UI: %d checks, %d failures\n",mods_checks,mods_failures);return prior || mods_failures;
}
