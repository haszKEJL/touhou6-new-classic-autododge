#pragma once
#include <array>
namespace i18n {
enum class Language { English, Polish, Russian };
inline Language valid(int value) { return value>=0 && value<3 ? Language(value):Language::English; }
struct Text {
    const char *hide, *language, *author, *version;
    std::array<const char*,4> names, descriptions;
    const char *armed, *enabled, *on, *off, *toggle, *hold, *unbound, *capture;
    const char *captureHelp, *holdHelp, *stopAll, *pausedHelp, *stats;
    const char *starting, *ready, *stopped, *error, *saveError;
    const char *prediction, *predictionHelp, *plannedRoute, *projectedMove;
    const char *scoreMode, *scoreModeHelp;
};
inline constexpr std::array<Text,3> translations{{
    {"Hide [Ins]", "Language", "Author", "Version",
     {"Auto dodge", "Item collection", "Autobomb", "Autoplay"},
     {"Steers away from incoming threats.", "Collects power, bombs and extra lives.", "Uses a bomb when escape is blocked.", "Dodges, shoots, collects and bombs."},
     "Armed", "Enabled", "ON", "OFF", "Toggle", "Hold", "Unbound", "Press a key...",
     "Press a key. Esc: cancel. Backspace: clear binding.",
     "Hold: arm the feature, then hold its key. Settings are saved automatically.",
     "Disable all [F9]", "Insert: menu. Automation pauses while this panel is open.",
     "Power %d/128  |  Bombs %d  |  Bullets %d  |  Lasers %d",
     "Starting...", "Ready", "Engine stopped. Restart the game.", "Error", "Could not save settings.",
     "Show planned movement", "Cyan: predicted path. Ring: goal. Red: no safe route.", "Planned route", "Direction forecast",
     "Score priority", "Collects items higher up and uses auto-collect when safe."},
    {"Ukryj [Ins]", "Język", "Autor", "Wersja",
     {"Automatyczny unik", "Zbieranie przedmiotów", "Autobomba", "Autoplay"},
     {"Koryguje ruch przy zagrożeniu.", "Zbiera moc, bomby i dodatkowe życia.", "Używa bomby, gdy nie ma ucieczki.", "Unika, strzela, zbiera i używa bomb."},
     "Uzbrojony", "Włączony", "WŁ.", "WYŁ.", "Przełącz", "Przytrzymaj", "Brak", "Wciśnij klawisz",
     "Naciśnij klawisz. Esc: anuluj. Backspace: usuń przypisanie.",
     "Przytrzymaj: uzbrój funkcję i trzymaj jej klawisz. Ustawienia zapisują się same.",
     "Wyłącz wszystko [F9]", "Insert: menu. Otwarty panel wstrzymuje automatykę.",
     "Moc %d/128  |  Bomby %d  |  Pociski %d  |  Lasery %d",
     "Uruchamianie...", "Gotowy", "Silnik zatrzymany. Uruchom grę ponownie.", "Błąd", "Nie można zapisać ustawień.",
     "Pokaż planowany ruch", "Turkus: trasa. Okrąg: cel. Czerwony: brak bezpiecznej trasy.", "Planowana trasa", "Prognoza kierunku",
     "Priorytet wyniku", "Zbiera przedmioty wyżej i korzysta z autoprzyciągania, gdy jest bezpiecznie."},
    {"Скрыть [Ins]", "Язык", "Автор", "Версия",
     {"Автоуклонение", "Сбор предметов", "Автобомба", "Автоигра"},
     {"Уклоняется от приближающихся угроз.", "Собирает силу, бомбы и жизни.", "Использует бомбу, если выхода нет.", "Уклоняется, стреляет и собирает."},
     "Готов", "Включено", "ВКЛ.", "ВЫКЛ.", "Переключать", "Удерживать", "Нет", "Клавиша...",
     "Нажмите клавишу. Esc: отмена. Backspace: удалить привязку.",
     "Удерживать: включите функцию и держите клавишу. Настройки сохраняются.",
     "Выключить всё [F9]", "Insert: меню. Пока панель открыта, автоматика на паузе.",
     "Сила %d/128  |  Бомбы %d  |  Пули %d  |  Лазеры %d",
     "Запуск...", "Готово", "Движок остановлен. Перезапустите игру.", "Ошибка", "Не удалось сохранить настройки.",
     "Показывать план движения", "Бирюзовый: путь. Круг: цель. Красный: нет безопасного пути.", "Планируемый путь", "Прогноз направления",
     "Приоритет счёта", "Собирает предметы выше и использует автосбор, когда безопасно."}
}};
inline const Text& get(Language language) { return translations[size_t(valid(int(language)))]; }
}

