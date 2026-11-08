#include "mods_page.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
extern "C" {
#include "mods.h"
#include "mod_settings.h"
}
#ifdef XCX_MOD_UI_TESTING
#include "imgui_internal.h"
struct ModsWidget {ImGuiID id;ImGuiWindow *window;ImVec2 low,high;char mod[65],field[65];};
static ModsWidget mods_widgets[128];static int mods_widget_count;
static ImVec2 mods_retry_low,mods_retry_high;static bool mods_retry_visible;
#endif
static const char *status_name(int status) {
    switch(status) {case MOD_LOADED:return "Loaded";case MOD_DISABLED:return "Disabled";case MOD_INCOMPATIBLE:return "Incompatible";case MOD_FAULTED:return "Faulted";default:return "Starting";}
}
void mods_page(float scale,float bottom,bool focus_first) {
#ifdef XCX_MOD_UI_TESTING
    mods_widget_count=0;mods_retry_visible=false;
#endif
    mod_ui_interaction(1);
    static ModSnapshot snapshot;mod_ui_snapshot(&snapshot);
    static char selected[65];
    if(!snapshot.count) {ImGui::TextWrapped("No mods configured. Add packages and edit enabled.txt, then restart.");return;}
    int index=0;for(int i=0;i<snapshot.count;++i)if(!strcmp(selected,snapshot.mods[i].id))index=i;
    snprintf(selected,sizeof selected,"%s",snapshot.mods[index].id);
    if(ImGui::BeginChild("##mods_page",ImVec2(0,ImGui::GetContentRegionAvail().y-bottom),ImGuiChildFlags_NavFlattened)) {
        if(ImGui::BeginTable("##mods_columns",2,ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##mod_list",ImGuiTableColumnFlags_WidthFixed,260*scale);
            ImGui::TableSetupColumn("##mod_settings",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            for(int i=0;i<snapshot.count;++i) {
                const ModView &mod=snapshot.mods[i];ImGui::PushID(mod.id);
                if(focus_first && i==index)ImGui::SetKeyboardFocusHere();
                if(ImGui::Selectable(mod.name[0]?mod.name:mod.id,i==index)) {index=i;snprintf(selected,sizeof selected,"%s",mod.id);}
                ImGui::TextDisabled("%s",status_name(mod.status));ImGui::Spacing();ImGui::PopID();
            }
            ImGui::TableNextColumn();const ModView &mod=snapshot.mods[index];ImGui::PushID(mod.id);
            ImGui::TextUnformatted(mod.name[0]?mod.name:mod.id);ImGui::TextDisabled("%s  %s",mod.id,mod.version);
            ImGui::Text("Status: %s",status_name(mod.status));
            if(mod.diagnostic[0])ImGui::TextWrapped("%s",mod.diagnostic);
            if(mod.title[0]) {ImGui::Separator();ImGui::TextUnformatted(mod.title);}
            ImGui::BeginDisabled(mod.status!=MOD_LOADED);
            for(int i=0;i<mod.field_count;++i) {
                const ModField &f=mod.fields[i];ImGui::PushID(f.id);ImGui::TextUnformatted(f.label);
                bool changed=false;int value=f.value;
                ImGui::SetNextItemWidth(-1);
                if(f.type==MOD_BOOL) {bool checked=value!=0;changed=ImGui::Checkbox("##value",&checked);value=checked;}
                else if(f.type==MOD_INTEGER)changed=ImGui::SliderInt("##value",&value,f.minimum,f.maximum,"%d",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput);
                else if(f.type==MOD_ENUM && ImGui::BeginCombo("##value",f.options[value].label)) {
                    for(int j=0;j<f.option_count;++j) {ImGui::PushID(f.options[j].id);if(ImGui::Selectable(f.options[j].label,j==value)){value=j;changed=true;}if(j==value)ImGui::SetItemDefaultFocus();ImGui::PopID();}
                    ImGui::EndCombo();
                }
#ifdef XCX_MOD_UI_TESTING
                if(mods_widget_count<128) {ModsWidget &w=mods_widgets[mods_widget_count++];w.id=GImGui->LastItemData.ID;w.window=ImGui::GetCurrentWindow();w.low=ImGui::GetItemRectMin();w.high=ImGui::GetItemRectMax();snprintf(w.mod,sizeof w.mod,"%s",mod.id);snprintf(w.field,sizeof w.field,"%s",f.id);}
#endif
                if(changed) {
                    ModAction a={};a.generation=snapshot.generation;a.session=snapshot.session;a.type=f.type;a.value=value;
                    snprintf(a.mod,sizeof a.mod,"%s",mod.id);snprintf(a.field,sizeof a.field,"%s",f.id);rt_mods_ui_submit(&a);
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            if(mod.save_note[0])ImGui::TextWrapped("%s",mod.save_note);
            if(mod.persistence_dirty && mod.field_count) {
                ImGui::BeginDisabled(mod.status!=MOD_LOADED);
                if(ImGui::Button("Retry save")) {
                    // A same-value action retries the complete page without repeating Lua events.
                    const ModField &f=mod.fields[0];ModAction a={};a.generation=snapshot.generation;a.session=snapshot.session;a.type=f.type;a.value=f.value;
                    snprintf(a.mod,sizeof a.mod,"%s",mod.id);snprintf(a.field,sizeof a.field,"%s",f.id);rt_mods_ui_submit(&a);
                }
#ifdef XCX_MOD_UI_TESTING
                mods_retry_visible=true;mods_retry_low=ImGui::GetItemRectMin();mods_retry_high=ImGui::GetItemRectMax();
#endif
                ImGui::EndDisabled();
            }
            if(snapshot.pending)ImGui::TextDisabled("Applying settings...");
            if(snapshot.notice[0])ImGui::TextWrapped("%s",snapshot.notice);
            ImGui::Separator();ImGui::TextWrapped("Package and enable/disable changes take effect after restart.");
            ImGui::PopID();ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}
