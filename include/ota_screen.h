#pragma once
#include <lvgl.h>
#include <Arduino.h>

// Schermata OTA persistente, senza flicker.
// Uso: OTAView::begin(); OTAView::show(); OTAView::setProgress(done,total,"phase"); OTAView::setStatus("...");

class OTAView {
public:
  // crea i widget se non esistono (idempotente)
  static void begin();

  // rende visibile la schermata (carica la screen una sola volta)
  static void show();

  // aggiorna la barra in modo "debounced" (solo quando cambia)
  static void setProgress(size_t done, size_t total, const char* phase);

  // aggiorna messaggio secondario
  static void setStatus(const char* msg);

  // mostra/nascondi avviso "NON SPEGNERE"
  static void showWarning(bool show = true);

  // resetta a 0%
  static void reset();

private:
  static lv_obj_t* screen_;
  static lv_obj_t* title_;
  static lv_obj_t* bar_;
  static lv_obj_t* percent_;
  static lv_obj_t* phase_;
  static lv_obj_t* warning_;  // Label avviso "NON SPEGNERE"
  static int lastPct_;
  static bool loaded_;
  static void ensureUI_();
};
