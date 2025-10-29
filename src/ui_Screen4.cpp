// src/ui_Screen4.cpp - Visualizzazione immagine JPG da SD
// SOLUZIONE B: Disegno diretto su LCD senza allocare RAM per l'immagine
#include "ui.h"
#include <SD.h>
#include <TJpg_Decoder.h>
#include <Arduino_GFX_Library.h>

// Dichiarazione esterna del display GFX (definito in main.cpp)
extern Arduino_GFX *gfx;

lv_obj_t * ui_Screen4;
lv_obj_t * ui_Screen4_Label_Status;

static bool jpg_display_active = false;

// Callback per TJpg_Decoder: disegna direttamente sull'LCD
static bool screen4_tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
    if (!jpg_display_active || !gfx) return false;

    // Disegna direttamente sull'LCD senza buffer intermedio
    gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);

    return true;
}

// Carica e disegna il JPG direttamente dall'SD all'LCD
static bool load_and_draw_jpg(const char* filepath)
{
    // Verifica che la SD sia disponibile
    if (!SD.begin()) {
        return false;
    }

    // Verifica che il file esista
    if (!SD.exists(filepath)) {
        return false;
    }

    // Configura TJpg_Decoder
    TJpgDec.setJpgScale(1);  // Nessuna scala (1:1)
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(screen4_tft_output);

    // Prepara il display per la scrittura
    if (gfx) {
        gfx->fillScreen(BLACK);  // Pulisci schermo
        jpg_display_active = true;

        // Disegna il JPG direttamente dalla SD all'LCD
        // drawSdJpg legge dalla SD e chiama la callback per ogni blocco decodificato
        int result = TJpgDec.drawSdJpg(0, 0, filepath);

        jpg_display_active = false;

        if (result == 0) {
            return true;  // Successo
        }
    }

    return false;
}

extern "C" {

void ui_Screen4_screen_init(void)
{
    // Nascondi la UI LVGL per mostrare solo il JPG
    // LVGL si sovrappone al display, quindi disabilitiamo il rendering LVGL
    lv_obj_t* act_scr = lv_scr_act();
    if (act_scr) {
        lv_obj_add_flag(act_scr, LV_OBJ_FLAG_HIDDEN);
    }

    // Carica e disegna l'immagine direttamente
    if (!load_and_draw_jpg("/vista.jpg")) {
        // Errore: riattiva LVGL per mostrare il messaggio di errore
        if (act_scr) {
            lv_obj_clear_flag(act_scr, LV_OBJ_FLAG_HIDDEN);
        }

        // Crea schermata di errore
        ui_Screen4 = lv_obj_create(NULL);
        lv_obj_clear_flag(ui_Screen4, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(ui_Screen4, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_Screen4_Label_Status = lv_label_create(ui_Screen4);
        lv_obj_set_width(ui_Screen4_Label_Status, LV_SIZE_CONTENT);
        lv_obj_set_height(ui_Screen4_Label_Status, LV_SIZE_CONTENT);
        lv_obj_set_align(ui_Screen4_Label_Status, LV_ALIGN_CENTER);
        lv_label_set_text(ui_Screen4_Label_Status, "Errore: vista.jpg non trovato su SD");
        lv_obj_set_style_text_color(ui_Screen4_Label_Status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_Screen4_Label_Status, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_disp_load_scr(ui_Screen4);
    } else {
        // Successo: l'immagine è già visualizzata, crea uno schermo LVGL vuoto e trasparente
        ui_Screen4 = lv_obj_create(NULL);
        lv_obj_clear_flag(ui_Screen4, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(ui_Screen4, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_Screen4_Label_Status = NULL;
    }
}

void ui_Screen4_screen_deinit(void)
{
    // Nessuna memoria da liberare - il JPG è stato disegnato direttamente
    jpg_display_active = false;

    // Reset della callback - la callback dello splash screen verrà reimpostata al prossimo boot
    TJpgDec.setCallback(nullptr);
}

} // extern "C"
