// ui_Screen3.c - SCHERMATA STATISTICHE VIAGGI CON GRAFICO BARRE
// Colori coordinati con Screen1: Arancione consumo, Verde rigenerazione

#include "ui.h"
#include "ui_helpers.h"
#include "canparser.h"

// === OGGETTI UI SCREEN3 ===
lv_obj_t * ui_Screen3;
lv_obj_t * ui_chart_trips;
lv_obj_t * ui_stats_panel;
lv_obj_t * ui_label_title;
lv_obj_t * ui_label_avg_consumption;
lv_obj_t * ui_label_avg_regen;
lv_obj_t * ui_label_total_trips;
lv_obj_t * ui_label_best_trip;
lv_obj_t * ui_label_back_hint;

// Serie grafici
lv_chart_series_t * series_consumption;
lv_chart_series_t * series_regen;

void ui_Screen3_screen_init(void) {
    // === SCREEN 3 - SFONDO NERO ===
    ui_Screen3 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen3, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);

    // === TITOLO ===
    ui_label_title = lv_label_create(ui_Screen3);
    lv_obj_set_width(ui_label_title, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_label_title, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_label_title, 0);
    lv_obj_set_y(ui_label_title, -220);
    lv_obj_set_align(ui_label_title, LV_ALIGN_CENTER);
    lv_label_set_text(ui_label_title, "STATISTICHE VIAGGI - Ultimi 10");
    lv_obj_set_style_text_color(ui_label_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_title, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // === LEGENDA GRAFICO ===
    lv_obj_t* ui_legend = lv_label_create(ui_Screen3);
    lv_obj_set_x(ui_legend, 0);
    lv_obj_set_y(ui_legend, -185);
    lv_obj_set_align(ui_legend, LV_ALIGN_CENTER);
    lv_label_set_text(ui_legend, "#FFA500 Consumo (Wh/km)#   #5CFC66 Rigenerazione (%)#");
    lv_obj_set_style_text_color(ui_legend, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_legend, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_recolor(ui_legend, true);  // Abilita color code #RRGGBB#

    // === GRAFICO PRINCIPALE (ULTIMI 10 VIAGGI) ===
    ui_chart_trips = lv_chart_create(ui_Screen3);
    lv_obj_set_width(ui_chart_trips, 700);
    lv_obj_set_height(ui_chart_trips, 280);
    lv_obj_set_x(ui_chart_trips, 0);
    lv_obj_set_y(ui_chart_trips, -20);
    lv_obj_set_align(ui_chart_trips, LV_ALIGN_CENTER);

    // Configurazione grafico
    lv_chart_set_type(ui_chart_trips, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(ui_chart_trips, 10);  // 10 viaggi
    lv_chart_set_range(ui_chart_trips, LV_CHART_AXIS_PRIMARY_Y, 0, 200);  // 0-200 Wh/km
    lv_chart_set_div_line_count(ui_chart_trips, 5, 10);  // Linee divisorie

    // Stile grafico - sfondo scuro trasparente
    lv_obj_set_style_bg_color(ui_chart_trips, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_chart_trips, 200, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_chart_trips, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_chart_trips, 2, LV_PART_MAIN);

    // Linee griglia
    lv_obj_set_style_line_color(ui_chart_trips, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_line_width(ui_chart_trips, 1, LV_PART_MAIN);

    // === SERIE 1: CONSUMO (ARANCIONE) ===
    series_consumption = lv_chart_add_series(ui_chart_trips, lv_color_hex(0xFFA500), LV_CHART_AXIS_PRIMARY_Y);

    // === SERIE 2: RIGENERAZIONE (VERDE) ===
    series_regen = lv_chart_add_series(ui_chart_trips, lv_color_hex(0x5CFC66), LV_CHART_AXIS_PRIMARY_Y);

    // === PANNELLO STATISTICHE ===
    ui_stats_panel = lv_obj_create(ui_Screen3);
    lv_obj_set_width(ui_stats_panel, 700);
    lv_obj_set_height(ui_stats_panel, 120);
    lv_obj_set_x(ui_stats_panel, 0);
    lv_obj_set_y(ui_stats_panel, 180);
    lv_obj_set_align(ui_stats_panel, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_stats_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_stats_panel, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_stats_panel, 200, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_stats_panel, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_stats_panel, 2, LV_PART_MAIN);

    // === LABEL STATISTICHE (COLONNE) ===
    // Consumo medio
    ui_label_avg_consumption = lv_label_create(ui_stats_panel);
    lv_obj_set_x(ui_label_avg_consumption, -240);
    lv_obj_set_y(ui_label_avg_consumption, -30);
    lv_obj_set_align(ui_label_avg_consumption, LV_ALIGN_CENTER);
    lv_label_set_text(ui_label_avg_consumption, "Consumo medio:\n---.- Wh/km");
    lv_obj_set_style_text_color(ui_label_avg_consumption, lv_color_hex(0xFFA500), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_label_avg_consumption, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui_label_avg_consumption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Rigenerazione media
    ui_label_avg_regen = lv_label_create(ui_stats_panel);
    lv_obj_set_x(ui_label_avg_regen, -80);
    lv_obj_set_y(ui_label_avg_regen, -30);
    lv_obj_set_align(ui_label_avg_regen, LV_ALIGN_CENTER);
    lv_label_set_text(ui_label_avg_regen, "Regen media:\n--.-% (--.- Wh)");
    lv_obj_set_style_text_color(ui_label_avg_regen, lv_color_hex(0x5CFC66), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_label_avg_regen, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui_label_avg_regen, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Viaggi totali
    ui_label_total_trips = lv_label_create(ui_stats_panel);
    lv_obj_set_x(ui_label_total_trips, 100);
    lv_obj_set_y(ui_label_total_trips, -30);
    lv_obj_set_align(ui_label_total_trips, LV_ALIGN_CENTER);
    lv_label_set_text(ui_label_total_trips, "Viaggi validi:\n--");
    lv_obj_set_style_text_color(ui_label_total_trips, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_label_total_trips, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui_label_total_trips, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Miglior viaggio
    ui_label_best_trip = lv_label_create(ui_stats_panel);
    lv_obj_set_x(ui_label_best_trip, 260);
    lv_obj_set_y(ui_label_best_trip, -30);
    lv_obj_set_align(ui_label_best_trip, LV_ALIGN_CENTER);
    lv_label_set_text(ui_label_best_trip, "Best trip:\n---.- Wh/km");
    lv_obj_set_style_text_color(ui_label_best_trip, lv_color_hex(0x00A0FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_label_best_trip, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui_label_best_trip, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // === HINT RITORNO ===
    ui_label_back_hint = lv_label_create(ui_Screen3);
    lv_obj_set_x(ui_label_back_hint, 0);
    lv_obj_set_y(ui_label_back_hint, 230);
    lv_obj_set_align(ui_label_back_hint, LV_ALIGN_CENTER);
    lv_label_set_text(ui_label_back_hint, "Premi il pulsante per tornare");
    lv_obj_set_style_text_color(ui_label_back_hint, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_label_back_hint, &lv_font_montserrat_14, LV_PART_MAIN);
}

// === FUNZIONE AGGIORNAMENTO DATI GRAFICO ===
void ui_Screen3_update_chart(void) {
    if (!ui_Screen3 || !ui_chart_trips) return;

    // Ottieni i dati dei viaggi dal CSV
    int tripCount = consumptionLearner.getValidTripsCount();

    if (tripCount == 0) {
        // Nessun dato disponibile
        lv_label_set_text(ui_label_avg_consumption, "Consumo medio:\nN/A");
        lv_label_set_text(ui_label_avg_regen, "Regen media:\nN/A");
        lv_label_set_text(ui_label_total_trips, "Viaggi validi:\n0");
        lv_label_set_text(ui_label_best_trip, "Best trip:\nN/A");

        // Azzera grafico
        lv_chart_set_all_value(ui_chart_trips, series_consumption, 0);
        lv_chart_set_all_value(ui_chart_trips, series_regen, 0);
        lv_chart_refresh(ui_chart_trips);
        return;
    }

    // === POPOLA GRAFICO CON ULTIMI 10 VIAGGI ===
    // Array per ordinare viaggi per timestamp (più vecchi a sinistra, più recenti a destra)
    typedef struct {
        float consumption;
        float regenPercent;  // Percentuale di rigenerazione
        unsigned long timestamp;
        int originalIndex;
    } ChartTrip;

    ChartTrip chartTrips[10];
    int chartCount = 0;

    // Raccogli tutti i viaggi validi
    for (int i = 0; i < 10; i++) {
        const TripData* trip = consumptionLearner.getTripData(i);
        if (trip) {
            chartTrips[chartCount].consumption = trip->consumption;
            chartTrips[chartCount].regenPercent = trip->regenPercent;  // Usa % invece di Wh assoluti
            chartTrips[chartCount].timestamp = trip->timestamp;
            chartTrips[chartCount].originalIndex = i;
            chartCount++;
        }
    }

    // Ordina per timestamp (più vecchio prima)
    for (int i = 0; i < chartCount - 1; i++) {
        for (int j = 0; j < chartCount - 1 - i; j++) {
            if (chartTrips[j].timestamp > chartTrips[j + 1].timestamp) {
                ChartTrip temp = chartTrips[j];
                chartTrips[j] = chartTrips[j + 1];
                chartTrips[j + 1] = temp;
            }
        }
    }

    // Popola il grafico (max 10 punti)
    // NOTA: Consumo in Wh/km (0-200), Rigenerazione in % (0-100) dello stesso asse
    for (int i = 0; i < 10; i++) {
        if (i < chartCount) {
            // Dati reali
            lv_chart_set_value_by_id(ui_chart_trips, series_consumption, i, (int)chartTrips[i].consumption);
            // Rigenera: converti % in valore comparabile (es. 15% → 15 sul grafico)
            lv_chart_set_value_by_id(ui_chart_trips, series_regen, i, (int)chartTrips[i].regenPercent);
        } else {
            // Riempi con zeri se meno di 10 viaggi
            lv_chart_set_value_by_id(ui_chart_trips, series_consumption, i, 0);
            lv_chart_set_value_by_id(ui_chart_trips, series_regen, i, 0);
        }
    }

    lv_chart_refresh(ui_chart_trips);

    // === AGGIORNA STATISTICHE ===
    float avgConsumption = consumptionLearner.getAverageConsumption();
    float avgRegenPercent = consumptionLearner.getAverageRegenPercent();
    float avgRegenEnergy = consumptionLearner.getAverageRegenEnergy();
    float bestConsumption = consumptionLearner.getBestConsumption();

    char buf[64];

    // Consumo medio
    snprintf(buf, sizeof(buf), "Consumo medio:\n%.1f Wh/km", avgConsumption);
    lv_label_set_text(ui_label_avg_consumption, buf);

    // Rigenerazione media
    if (avgRegenPercent > 0 || avgRegenEnergy > 0) {
        snprintf(buf, sizeof(buf), "Regen media:\n%.1f%% (%.0f Wh)", avgRegenPercent, avgRegenEnergy);
    } else {
        snprintf(buf, sizeof(buf), "Regen media:\nN/A");
    }
    lv_label_set_text(ui_label_avg_regen, buf);

    // Viaggi totali
    snprintf(buf, sizeof(buf), "Viaggi validi:\n%d", tripCount);
    lv_label_set_text(ui_label_total_trips, buf);

    // Miglior viaggio
    if (bestConsumption > 0) {
        snprintf(buf, sizeof(buf), "Best trip:\n%.1f Wh/km", bestConsumption);
    } else {
        snprintf(buf, sizeof(buf), "Best trip:\nN/A");
    }
    lv_label_set_text(ui_label_best_trip, buf);
}
