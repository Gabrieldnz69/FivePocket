#pragma once
// imenu_view_adapter.h (PROPOSED, lane upd1-imenu/draw-glue): the join between the model lane's recomp::imenu::Model
// (upd1-imenu/model-rows imenu_model.h: rows, pages, cursor, hold / close / cool-down rules, controls, the sounds of a tick) and this
// lane's drawing half (imenu_draw.h Drawer: assets, header, list, buttons, sounds in am_pi_menu's order, guard, release).
// Use it when the model lane's Model owns the frame (both lanes implement the frame rules; NOTES.md 6 lists the choice for the lead):
//
//     const bool drawNow=model.tick(nat,ctx,req);   // the model's own controls / input / cool-down
//     frame.after(nat,model,drawNow);                // this file: sounds, header, list, release on close
//
// Pure: fixed storage, no allocation; converts a View into a menudraw::Page once per drawn frame (pointer copies only).
#include "imenu_model.h"
#include "imenu_draw.h"

namespace recomp::imenudraw {

// The model's text kinds -> a menudraw cell. Label / LabelInt are GXT keys (+ one integer); Literal goes through STRING + substring.
// Guest (a game char* such as the player name) has no host string: it is shown only in the header, which the Drawer reads itself.
inline void cellFrom(const imenu::Text& t,const char*& s,bool& key,bool* hasInt,int* v) noexcept {
    s=nullptr;key=false;if(hasInt)*hasInt=false;if(v)*v=0;
    switch(t.kind){
    case imenu::Text::Label:s=t.s;key=true;break;
    case imenu::Text::LabelInt:s=t.s;key=true;if(hasInt)*hasInt=true;if(v)*v=t.n;break;
    case imenu::Text::Literal:s=t.s;break;
    default:break;
    }
}
// am_pi_menu's instructional-button icons (0x259b5 -> 0x25da2): 30 with the accept labels (PIM_CSEL / CUSE / CPLY / CNEX), 31 with
// PIM_CEXI. Other controls have no observed icon: such a button is left out (INFERRED: the model lane offers none).
inline int iconFor(int control) noexcept {return control==imenu::CtlAccept?30:control==imenu::CtlCancel?31:-1;}

class ViewPage {
public:
    const menudraw::Page& from(const imenu::View& v) noexcept {
        const int count=v.count<0?0:(v.count>menudraw::Renderer::MaxRows?menudraw::Renderer::MaxRows:v.count);
        for(int i=0;i<count;++i){
            const imenu::RowView& r=v.rows[i];menudraw::Row& o=rows_[i];
            o=menudraw::Row{nullptr};
            cellFrom(r.label,o.label,o.labelKey,nullptr,nullptr);if(!o.label)o.label="";
            bool hasInt=false;int n=0;cellFrom(r.value,o.value,o.valueKey,&hasInt,&n);o.valueHasInt=hasInt;o.valueInt=n;
            o.enabled=r.enabled;o.cycles=r.cycles;
        }
        int nb=0;
        for(int i=0;i<v.buttonCount && i<3;++i){const int icon=iconFor(v.buttons[i].control);if(icon>=0)buttons_[nb++]={icon,v.buttons[i].label};}
        page_=menudraw::Page{};
        page_.header=nullptr;page_.rows=rows_;page_.rowCount=count;page_.selected=v.selected;
        cellFrom(v.title,page_.title,page_.titleKey,nullptr,nullptr);
        cellFrom(v.help,page_.help,page_.helpKey,nullptr,nullptr);
        page_.buttons=buttons_;page_.buttonCount=nb;
        return page_;
    }
private:
    menudraw::Row rows_[menudraw::Renderer::MaxRows]{};menudraw::Button buttons_[3]{};menudraw::Page page_{};
};

// After the model's tick, every Sandbox frame: draws while open, plays the tick's sounds in the disc's order (navigation before the
// draw, SELECT / ERROR after it, BACK / CANCEL after HIDE_HELP_TEXT_THIS_FRAME), releases once when the model closed.
class ModelFrame {
public:
    Drawer& drawer() noexcept {return d_;}
    void after(Natives& nat,const imenu::Model& m,bool drawNow) noexcept {
        if(m.open()){
            const bool ready=d_.load(nat);
            play(nat,m,[](imenu::Sound s){return s==imenu::Sound::UpDown || s==imenu::Sound::LeftRight;});
            if(ready && drawNow)d_.draw(nat,page_.from(m.view()));
            play(nat,m,[](imenu::Sound s){return s==imenu::Sound::Select || s==imenu::Sound::Error;});
            Drawer::hideHelp(nat);
            play(nat,m,[](imenu::Sound s){return s==imenu::Sound::Back || s==imenu::Sound::Cancel;});
            wasOpen_=true;
        } else if(wasOpen_){
            play(nat,m,[](imenu::Sound){return true;});   // the closing tick's CANCEL / SELECT
            d_.release(nat,m.lastClose());wasOpen_=false;
        }
    }
private:
    template<class P> static void play(Natives& nat,const imenu::Model& m,P pick) noexcept {
        for(int i=0;i<m.soundCount();++i)   // v989: no ERROR sound exists in am_pi_menu (draw-glue NOTES 3): a refusal is silent
            if(m.sound(i)!=imenu::Sound::Error && pick(m.sound(i)))Drawer::sound(nat,imenu::soundName(m.sound(i)));
    }
    Drawer d_{};ViewPage page_{};bool wasOpen_=false;
};

} // namespace recomp::imenudraw
