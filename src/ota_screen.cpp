#include "ota_screen.h"
#include <cstdio>

lv_obj_t* OTAView::screen_  = nullptr;
lv_obj_t* OTAView::title_   = nullptr;
lv_obj_t* OTAView::bar_     = nullptr;
lv_obj_t* OTAView::percent_ = nullptr;
lv_obj_t* OTAView::phase_   = nullptr;
lv_obj_t* OTAView::warning_ = nullptr;
int       OTAView::lastPct_ = -1;
bool      OTAView::loaded_  = false;

static inline int clampPct_(int v){ if(v<0) return 0; if(v>100) return 100; return v; }

void OTAView::ensureUI_() {
  if (screen_) return;

  // Screen
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x0E1322), 0);
  lv_obj_set_style_bg_grad_color(screen_, lv_color_hex(0x202B5A), 0);
  lv_obj_set_style_bg_grad_dir(screen_, LV_GRAD_DIR_VER, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  // Titolo
  title_ = lv_label_create(screen_);
  lv_label_set_text(title_, "Aggiornamento firmware");
  lv_obj_set_style_text_color(title_, lv_color_hex(0xEAF2FF), 0);
  lv_obj_set_style_text_font(title_, LV_FONT_DEFAULT, 0);
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 24);

  // Barra
  bar_ = lv_bar_create(screen_);
  lv_obj_set_size(bar_, 260, 18);
  lv_obj_align(bar_, LV_ALIGN_CENTER, 0, 0);
  lv_bar_set_range(bar_, 0, 100);
  lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
  // stile barra
  lv_obj_set_style_bg_color(bar_, lv_color_hex(0x243152), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_, lv_color_hex(0x4DA3FF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(bar_, lv_color_hex(0x8FD3FF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(bar_, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

  // Percentuale
  percent_ = lv_label_create(screen_);
  lv_label_set_text(percent_, "0%");
  lv_obj_set_style_text_color(percent_, lv_color_hex(0xBFD7FF), 0);
  lv_obj_align_to(percent_, bar_, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  // Phase/Status
  phase_ = lv_label_create(screen_);
  lv_label_set_text(phase_, "Pronto…");
  lv_obj_set_style_text_color(phase_, lv_color_hex(0x9FB7E5), 0);
  lv_obj_align(phase_, LV_ALIGN_CENTER, 0, 36);

  // Warning message (nascosto di default)
  warning_ = lv_label_create(screen_);
  lv_label_set_text(warning_, "⚠ NON SPEGNERE IL VEICOLO");
  lv_obj_set_style_text_color(warning_, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_style_text_font(warning_, LV_FONT_DEFAULT, 0);
  lv_obj_align(warning_, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_add_flag(warning_, LV_OBJ_FLAG_HIDDEN);  // Nascosto all'inizio

  lastPct_ = -1;
  loaded_  = false;
}

void OTAView::begin() {
  ensureUI_();
}

void OTAView::show() {
  ensureUI_();
  if (!loaded_) {
    lv_scr_load(screen_);
    loaded_ = true;
  }
}

void OTAView::setProgress(size_t done, size_t total, const char* phase) {
  ensureUI_();
  int pct = 0;
  if (total > 0) {
    // calcolo safe evitando overflow
    pct = (int)((done * 100ull) / total);
  }
  pct = clampPct_(pct);

  // aggiorno solo se cambia
  if (pct != lastPct_) {
    lv_bar_set_value(bar_, pct, LV_ANIM_ON);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(percent_, buf);
    lastPct_ = pct;
  }

  if (phase && *phase) {
    // testo breve (max ~30 char)
    String p = phase;
    if (p.length() > 30) p = p.substring(0, 30);
    lv_label_set_text(phase_, p.c_str());
  }
}

void OTAView::setStatus(const char* msg) {
  ensureUI_();
  if (!msg) msg = "";
  String m = msg;
  if (m.length() > 40) m = m.substring(0, 40);
  lv_label_set_text(phase_, m.c_str());
}

void OTAView::showWarning(bool show) {
  ensureUI_();
  if (show) {
    lv_obj_clear_flag(warning_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(warning_, LV_OBJ_FLAG_HIDDEN);
  }
}

void OTAView::reset() {
  ensureUI_();
  lastPct_ = -1;
  lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
  lv_label_set_text(percent_, "0%");
  lv_label_set_text(phase_, "Pronto…");
  lv_obj_add_flag(warning_, LV_OBJ_FLAG_HIDDEN);  // Nascondi warning
}
