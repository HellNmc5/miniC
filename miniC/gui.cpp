// ============================================================================
//  gui.cpp — интерфейс лексического анализатора miniC (Dear ImGui).
//
//  От лексера используется: Lexer(const std::string&), nextToken(), поля Token,
//  codeName(), className(). Подсветку синтаксиса в редакторе тоже делает
//  лексер: редактор отдаёт ему строку и красит то, что вернул nextToken.
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "TextEditor.h"
#include "gui.h"
#include "Lexer.h"
#include "Parser.h"

#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace {

using PI = TextEditor::PaletteIndex;

// ================================================================= цвета
// Цвета классов лексем — одни и те же в редакторе и в таблице.

constexpr ImU32 rgb(unsigned c, unsigned a = 255) {
    return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a);
}

const ImU32 BG = rgb(0x1E232A), PANEL = rgb(0x232932), EDITOR_BG = rgb(0x1A1F25);
const ImU32 RAISED = rgb(0x2B323C), HOVER = rgb(0x353D49), ACTIVE = rgb(0x3E4755), BORDER = rgb(0x333B46);
const ImU32 TEXT = rgb(0xDCE1E7), MUTED = rgb(0x8B95A3), ACCENT = rgb(0x4FA3E0), ACCENT_TEXT = rgb(0x0E1A24);
const ImU32 KEYWORD = rgb(0x6CB6FF), CONSTANT = rgb(0x7ED3B2), OPERATOR = rgb(0xC8A2F0);
const ImU32 SEPARATOR = rgb(0x97A1AE), ERROR_FG = rgb(0xFF7B72), COMMENT = rgb(0x6C7888);
const ImU32 ERROR_BG = rgb(0xFF7B72, 34), SELECTION = rgb(0x4FA3E0, 70);

ImU32 classColor(TokenClass cls) {
    switch (cls) {
    case TokenClass::Keyword:   return KEYWORD;
    case TokenClass::Const:     return CONSTANT;
    case TokenClass::OpSign:    return OPERATOR;
    case TokenClass::Separator: return SEPARATOR;
    case TokenClass::Mistake:   return ERROR_FG;
    default:                    return TEXT;
    }
}

// Ячейки палитры редактора. Своих имён для операций, разделителей и ошибок
// в ней нет, поэтому заняты свободные: CharLiteral и String.
PI paletteFor(TokenClass cls) {
    switch (cls) {
    case TokenClass::Keyword:   return PI::Keyword;
    case TokenClass::Const:     return PI::Number;
    case TokenClass::OpSign:    return PI::Punctuation;
    case TokenClass::Separator: return PI::CharLiteral;
    case TokenClass::Mistake:   return PI::String;
    default:                    return PI::Identifier;
    }
}

// ================================================================= данные

struct Row {
    std::vector<std::string> cells;
    TokenClass cls = TokenClass::Identificator;
    bool isError = false;
    int line = 0, col = 0, len = 0;   // позиция в байтах, как у лексера
};

enum Tab { TAB_TOKENS, TAB_IDS, TAB_ERRORS, TAB_COUNT };

struct App {
    HWND hwnd = nullptr;
    float scale = 1.0f;
    ImFont* bold = nullptr;
    ImFont* code = nullptr;

    TextEditor editor;
    bool live = true;          // анализ при вводе
    bool dirty = false;
    double changedAt = -1.0;
    std::wstring path;

    std::vector<Row> rows[TAB_COUNT];
    int selected[TAB_COUNT] = { -1, -1, -1 };
    int tokenCount = 0, errorCount = 0;
    std::string problem;       // лексер завис или упал

    NodePtr tree;                        // дерево разбора последнего анализа
    std::string syntaxError;             // синтаксическая ошибка, если была
    const Node* selectedNode = nullptr;  // узел, выделенный во вкладке «Дерево»

    std::vector<std::string> lines;
    float split = 0.42f;       // доля ширины под редактор
    std::string idStats;
};

App g;

// ================================================================ утилиты

std::string toUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string fileName() {
    if (g.path.empty()) return "Без имени";
    size_t p = g.path.find_last_of(L"\\/");
    return toUtf8(p == std::wstring::npos ? g.path : g.path.substr(p + 1));
}

// Лексер считает колонки в байтах, редактор — в видимых позициях:
// кириллическая буква — два байта, табуляция — до следующего стопа.
int visualColumn(const std::string& line, int byteCol) {
    const int tab = g.editor.GetTabSize();
    int col = 0;
    for (size_t i = 0; i < line.size() && i + 1 < (size_t)byteCol; ) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\t') { col = (col / tab + 1) * tab; ++i; }
        else { ++col; i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }
    }
    return col;
}

// Одиночный байт кириллической буквы сам по себе не печатается — показываем код.
std::string displayLexeme(const std::string& text) {
    if (text.size() == 1 && (unsigned char)text[0] >= 0x80) {
        char buf[16];
        snprintf(buf, sizeof(buf), "байт 0x%02X", (unsigned char)text[0]);
        return buf;
    }
    return text;
}

std::string positionText(const Token& t) {
    return t.colStart == t.colEnd ? std::to_string(t.colStart)
                                  : std::to_string(t.colStart) + "–" + std::to_string(t.colEnd);
}

void textColored(ImU32 color, const char* s) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
}

// ===================================================== подсветка лексером

// Редактор вызывает это для каждой строки: «найди первую лексему в [begin, end)».
bool tokenizeWithLexer(const char* begin, const char* end,
                       const char*& outBegin, const char*& outEnd, PI& color) {
    const std::string chunk(begin, end);
    outBegin = begin;
    outEnd = end;
    color = PI::Default;
    try {
        Token t = Lexer(chunk).nextToken();
        size_t offset = (size_t)(t.colStart - 1);
        if (t.code == TokenCode::EndOfFile || t.line != 1 || offset >= chunk.size()) return true;
        outBegin = begin + offset;
        outEnd = outBegin + std::min(std::max<size_t>(1, t.text.size()), chunk.size() - offset);
        color = paletteFor(t.cls);
    } catch (...) {
        // исключение лексера — просто оставляем строку без цвета
    }
    return true;
}

void setupEditor() {
    TextEditor::LanguageDefinition lang;
    lang.mName = "miniC";
    lang.mSingleLineComment = "//";
    lang.mCommentStart = "\x01\x01";   // блочных комментариев и препроцессора
    lang.mCommentEnd = "\x02\x02";     // в miniC нет — ставим то, чего не бывает
    lang.mPreprocChar = '\x01';
    lang.mTokenize = tokenizeWithLexer;
    g.editor.SetLanguageDefinition(lang);
    g.editor.SetTabSize(4);
    g.editor.SetShowWhitespaces(false);

    TextEditor::Palette p = TextEditor::GetDarkPalette();
    p[(int)PI::Default] = TEXT;          p[(int)PI::Identifier] = TEXT;
    p[(int)PI::Keyword] = KEYWORD;       p[(int)PI::Number] = CONSTANT;
    p[(int)PI::Punctuation] = OPERATOR;  p[(int)PI::CharLiteral] = SEPARATOR;
    p[(int)PI::String] = ERROR_FG;       p[(int)PI::Comment] = COMMENT;
    p[(int)PI::Background] = EDITOR_BG;  p[(int)PI::Cursor] = TEXT;
    p[(int)PI::Selection] = SELECTION;   p[(int)PI::ErrorMarker] = rgb(0xFF7B72, 30);
    p[(int)PI::LineNumber] = rgb(0x56606D);
    p[(int)PI::CurrentLineFill] = p[(int)PI::CurrentLineFillInactive] = rgb(0xFFFFFF, 10);
    p[(int)PI::CurrentLineEdge] = 0;
    g.editor.SetPalette(p);
}

void setupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(10, 6);
    s.CellPadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 8);
    s.ScrollbarSize = 12;
    s.WindowRounding = 0;
    s.ChildRounding = 8;
    s.FrameRounding = s.PopupRounding = s.TabRounding = 6;
    s.ScrollbarRounding = s.GrabRounding = 6;
    s.WindowBorderSize = s.FrameBorderSize = 0;
    s.TabBarOverlineSize = 2;
    s.ScaleAllSizes(g.scale);

    struct { ImGuiCol id; ImU32 color; } colors[] = {
        { ImGuiCol_Text, TEXT },           { ImGuiCol_TextDisabled, MUTED },
        { ImGuiCol_WindowBg, BG },         { ImGuiCol_ChildBg, PANEL },        { ImGuiCol_PopupBg, PANEL },
        { ImGuiCol_MenuBarBg, BG },        { ImGuiCol_Border, BORDER },
        { ImGuiCol_FrameBg, RAISED },      { ImGuiCol_FrameBgHovered, HOVER }, { ImGuiCol_FrameBgActive, ACTIVE },
        { ImGuiCol_Button, RAISED },       { ImGuiCol_ButtonHovered, HOVER },  { ImGuiCol_ButtonActive, ACTIVE },
        { ImGuiCol_Header, SELECTION },    { ImGuiCol_HeaderHovered, HOVER },  { ImGuiCol_HeaderActive, ACTIVE },
        { ImGuiCol_CheckMark, ACCENT },    { ImGuiCol_Separator, BORDER },
        { ImGuiCol_Tab, PANEL },           { ImGuiCol_TabHovered, HOVER },     { ImGuiCol_TabSelected, RAISED },
        { ImGuiCol_TabSelectedOverline, ACCENT },
        { ImGuiCol_TableHeaderBg, RAISED },{ ImGuiCol_TableBorderLight, BORDER },
        { ImGuiCol_TableBorderStrong, BORDER },
        { ImGuiCol_TableRowBg, 0 },        { ImGuiCol_TableRowBgAlt, rgb(0xFFFFFF, 6) },
        { ImGuiCol_ScrollbarBg, 0 },       { ImGuiCol_ScrollbarGrab, ACTIVE },
        { ImGuiCol_TextSelectedBg, SELECTION },
    };
    for (const auto& c : colors) s.Colors[c.id] = ImGui::ColorConvertU32ToFloat4(c.color);

    BOOL dark = TRUE;   // тёмный заголовок окна в Windows 10/11
    DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.AddText("–—…№");
    b.BuildRanges(&ranges);

    wchar_t win[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(win, MAX_PATH);
    auto load = [&](std::initializer_list<const wchar_t*> names) -> ImFont* {
        for (const wchar_t* n : names) {
            std::wstring p = std::wstring(win) + L"\\Fonts\\" + n;
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                return io.Fonts->AddFontFromFileTTF(toUtf8(p).c_str(), 16.0f * g.scale, nullptr, ranges.Data);
        }
        return nullptr;
    };
    ImFont* ui = load({ L"segoeui.ttf" });           // первый загруженный — шрифт по умолчанию
    if (!ui) ui = io.Fonts->AddFontDefault();
    g.bold = load({ L"seguisb.ttf", L"segoeuib.ttf" });
    g.code = load({ L"CascadiaMono.ttf", L"consola.ttf" });
    if (!g.bold) g.bold = ui;
    if (!g.code) g.code = ui;
}

// ================================================================= анализ

void selectInEditor(const Row& r) {
    if (r.line < 1 || r.line > (int)g.lines.size()) return;
    const std::string& text = g.lines[r.line - 1];
    TextEditor::Coordinates a(r.line - 1, visualColumn(text, r.col));
    TextEditor::Coordinates b(r.line - 1, visualColumn(text, r.col + r.len));
    g.editor.SetCursorPosition(b);
    g.editor.SetSelection(a, b);
}

void analyze() {
    g.changedAt = -1.0;
    const std::string source = g.editor.GetText();
    g.lines = g.editor.GetTextLines();

    // 1. Прогон лексера с защитой от зависания и исключений
    std::vector<Token> tokens;
    g.problem.clear();
    Lexer lexer(source);
    try {
        for (Token t = lexer.nextToken(); t.code != TokenCode::EndOfFile; t = lexer.nextToken()) {
            if (!tokens.empty() && tokens.back().line == t.line && tokens.back().colStart == t.colStart) {
                g.problem = "Лексер не продвигается: nextToken дважды вернул лексему с позиции " +
                            std::to_string(t.line) + ":" + std::to_string(t.colStart);
                break;
            }
            tokens.push_back(t);
        }
    } catch (const std::exception& e) {
        g.problem = std::string("Лексер завершился с исключением: ") + e.what();
    }

    // 2. Раскладка по таблицам
    for (auto& r : g.rows) r.clear();
    for (int& s : g.selected) s = -1;
    TextEditor::ErrorMarkers markers;

    
    
    g.errorCount = 0;

    for (size_t n = 0; n < tokens.size(); ++n) {
        const Token& t = tokens[n];
        const bool isError = t.code == TokenCode::Error;
        const std::string msg = t.message.empty() ? codeName(t.code) : t.message;

        std::string value;

        if (t.cls == TokenClass::Identificator) {
            value = "№ " + std::to_string(t.value);
        }
        else if (t.cls == TokenClass::Const) {
            value = std::to_string(t.value);
        }

        const int len = (int)std::max<size_t>(1, t.text.size());

        g.rows[TAB_TOKENS].push_back({ { std::to_string(n + 1), displayLexeme(t.text), className(t.cls), msg,
            std::to_string((int)t.code), std::to_string(t.line), positionText(t), value },
            t.cls, isError, t.line, t.colStart, len });

        if (isError) {
            ++g.errorCount;
            g.rows[TAB_ERRORS].push_back({ { std::to_string(g.errorCount), std::to_string(t.line),
                positionText(t), displayLexeme(t.text), msg }, t.cls, true, t.line, t.colStart, len });
            std::string& m = markers[t.line];
            m += (m.empty() ? "" : "\n") + msg + ": " + displayLexeme(t.text) + ", позиция " + positionText(t);
        }
    }

    const auto& ids = lexer.identifiers().entries();
    for (size_t i = 0; i < ids.size(); ++i) {
        const IdEntry& e = ids[i];
        g.rows[TAB_IDS].push_back({ { std::to_string(i + 1), e.name,
            "стр " + std::to_string(e.line) + ", поз " + std::to_string(e.col), std::to_string(e.count) },
            TokenClass::Identificator, false, e.line, e.col, (int)e.name.size() });
    }
    g.idStats = "коллизий: " + std::to_string(lexer.identifiers().collisions()) +
                ", сравнений: " + std::to_string(lexer.identifiers().comparisons());
    // 3. Синтаксический анализ того же текста
    g.tree.reset();
    g.syntaxError.clear();
    g.selectedNode = nullptr;            // старое дерево удалено — указатель на его узел больше недействителен
    try {
        Parser parser(source);
        g.tree = parser.parseProgram();
    } catch (const SyntaxError& e) {
        g.syntaxError = e.message;
        ++g.errorCount;
        g.rows[TAB_ERRORS].push_back({ { std::to_string(g.errorCount), std::to_string(e.line),
            std::to_string(e.col), "", "синтаксис: " + e.message },
            TokenClass::Mistake, true, e.line, e.col, 1 });
        std::string& m = markers[e.line];
        m += (m.empty() ? "" : "\n") + e.message;
    } catch (const std::exception& e) {
        g.syntaxError = std::string("Парсер завершился с исключением: ") + e.what();
    }
    g.tokenCount = (int)tokens.size();
    g.editor.SetErrorMarkers(markers);
}

// ================================================================== файлы

void updateTitle() {
    SetWindowTextW(g.hwnd, toWide(fileName() + (g.dirty ? " (изменён)" : "") + " — miniC").c_str());
}

bool loadFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string bytes(GetFileSize(f, nullptr), '\0');
    DWORD got = 0;
    if (!bytes.empty()) ReadFile(f, &bytes[0], (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(f);
    bytes.resize(got);

    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);   // BOM
    bytes.erase(std::remove(bytes.begin(), bytes.end(), '\r'), bytes.end());

    g.editor.SetText(bytes);
    g.path = path;
    g.dirty = false;
    updateTitle();
    analyze();
    return true;
}

bool saveFile(bool askPath) {
    std::wstring path = g.path;
    if (askPath || path.empty()) {
        wchar_t buf[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g.hwnd;
        ofn.lpstrFilter = L"Программы miniC (*.mc)\0*.mc\0Все файлы\0*.*\0";
        ofn.lpstrFile = buf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"mc";
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn)) return false;
        path = buf;
    }

    std::string bytes;   // UTF-8 без BOM, переводы строк Windows
    for (char c : g.editor.GetText()) {
        if (c == '\n') bytes += '\r';
        bytes += c;
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    bool ok = f != INVALID_HANDLE_VALUE &&
              WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) {
        MessageBoxW(g.hwnd, (L"Не удалось сохранить файл:\n" + path).c_str(), L"miniC", MB_ICONWARNING);
        return false;
    }
    g.path = path;
    g.dirty = false;
    updateTitle();
    return true;
}

bool confirmDiscard() {
    if (!g.dirty) return true;
    int r = MessageBoxW(g.hwnd, L"Сохранить изменения?", L"miniC", MB_YESNOCANCEL | MB_ICONQUESTION);
    return r == IDNO || (r == IDYES && saveFile(false));
}

void openFile() {
    if (!confirmDiscard()) return;
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Программы miniC (*.mc)\0*.mc\0Все файлы\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn) && !loadFile(buf))
        MessageBoxW(g.hwnd, L"Не удалось открыть файл.", L"miniC", MB_ICONWARNING);
}

// ============================================================== отрисовка

void drawMenu() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("Файл")) {
        if (ImGui::MenuItem("Открыть…", "Ctrl+O")) openFile();
        if (ImGui::MenuItem("Сохранить", "Ctrl+S")) saveFile(false);
        if (ImGui::MenuItem("Сохранить как…", "Ctrl+Shift+S")) saveFile(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Выход")) PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Анализ")) {
        if (ImGui::MenuItem("Запустить", "F5")) analyze();
        ImGui::MenuItem("При вводе", nullptr, &g.live);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Вид")) {
        bool ws = g.editor.IsShowingWhitespaces();
        if (ImGui::MenuItem("Показывать пробелы", nullptr, ws)) g.editor.SetShowWhitespaces(!ws);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void drawToolbar() {
    if (ImGui::Button("Открыть")) openFile();
    ImGui::SameLine();
    if (ImGui::Button("Сохранить")) saveFile(false);
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(0x4FA3E0, 230));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x4FA3E0, 200));
    ImGui::PushStyleColor(ImGuiCol_Text, ACCENT_TEXT);
    if (ImGui::Button("Анализ  F5")) analyze();
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2);
    ImGui::Checkbox("При вводе", &g.live);

    const std::string name = fileName();
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(name.c_str()).x - ImGui::GetStyle().WindowPadding.x);
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(g.bold);
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopFont();
}

void drawEditor(float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##editorPane", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::SetCursorPos(ImVec2(s.WindowPadding.x, s.FramePadding.y));
    ImGui::PushFont(g.bold);
    ImGui::TextUnformatted("Программа");
    ImGui::PopFont();
    auto cur = g.editor.GetCursorPosition();
    std::string pos = "Стр " + std::to_string(cur.mLine + 1) + ", кол " + std::to_string(cur.mColumn + 1);
    ImGui::SameLine(width - ImGui::CalcTextSize(pos.c_str()).x - s.WindowPadding.x);
    textColored(MUTED, pos.c_str());

    ImGui::PushFont(g.code);
    g.editor.Render("##code");
    ImGui::PopFont();

    if (g.editor.IsTextChanged()) {
        if (!g.dirty) { g.dirty = true; updateTitle(); }
        g.changedAt = ImGui::GetTime();
    }
    ImGui::EndChild();
}

void drawSplitter(float total, float height) {
    const float w = 10.0f * g.scale;
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##split", ImVec2(w, height));
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
        g.split = std::min(0.75f, std::max(0.25f, g.split + ImGui::GetIO().MouseDelta.x / total));

    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    float x = (a.x + b.x) / 2, y = (a.y + b.y) / 2, half = 18.0f * g.scale;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y - half), ImVec2(x, y + half), hot ? ACCENT : BORDER, 3.0f * g.scale);
    ImGui::SameLine(0, 0);
}

// weight > 0 — колонка растягивается, 0 — узкая, по ширине заголовка
void drawTable(const char* id, Tab tab, std::initializer_list<std::pair<const char*, float>> cols) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable(id, (int)cols.size(), flags)) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    for (const auto& c : cols)
        ImGui::TableSetupColumn(c.first, c.second > 0 ? ImGuiTableColumnFlags_WidthStretch
                                                      : ImGuiTableColumnFlags_WidthFixed, c.second);
    ImGui::TableHeadersRow();

    const std::vector<Row>& rows = g.rows[tab];
    ImGuiListClipper clipper;
    clipper.Begin((int)rows.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Row& r = rows[i];
            ImGui::TableNextRow();
            if (r.isError) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ERROR_BG);
            ImGui::PushStyleColor(ImGuiCol_Text, tab == TAB_IDS ? TEXT : classColor(r.cls));

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (ImGui::Selectable(r.cells[0].c_str(), g.selected[tab] == i, ImGuiSelectableFlags_SpanAllColumns)) {
                g.selected[tab] = i;
                selectInEditor(r);
            }
            ImGui::PopID();
            for (size_t c = 1; c < r.cells.size(); ++c) {
                ImGui::TableSetColumnIndex((int)c);
                ImGui::TextUnformatted(r.cells[c].c_str());
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndTable();
}

// ========================================================== дерево разбора

// Цвета узлов — те же, что у классов лексем: операторы языка синие,
// операции фиолетовые, константы зелёные, остальное — обычным текстом
ImU32 nodeColor(NodeKind k) {
    switch (k) {
    case NodeKind::Program: case NodeKind::Function: case NodeKind::Param:
    case NodeKind::Block:   case NodeKind::Var:
        return TEXT;
    case NodeKind::Binary:  case NodeKind::Unary:
        return OPERATOR;
    case NodeKind::Const:
        return CONSTANT;
    default:                                        // объявление, присваивание, вызов, if, while, return
        return KEYWORD;
    }
}

// Подпись узла — как в консольном printTree
std::string nodeLabel(const Node& n) {
    std::string s = nodeName(n.kind);
    if (!n.token.text.empty()) s += "  " + n.token.text;
    if (n.type != TokenCode{}) s += "  : " + codeName(n.type);
    return s;
}

// Один узел и, рекурсивно, его дети
void drawNode(const Node& n) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (n.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (&n == g.selectedNode) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushStyleColor(ImGuiCol_Text, nodeColor(n.kind));
    const bool open = ImGui::TreeNodeEx("node", flags, "%s", nodeLabel(n).c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {   // щелчок по подписи, а не по стрелке
        g.selectedNode = &n;
        Row r;
        r.line = n.token.line;
        r.col = n.token.colStart;
        r.len = (int)std::max<size_t>(1, n.token.text.size());
        selectInEditor(r);
    }

    if (open && !n.children.empty()) {
        for (size_t i = 0; i < n.children.size(); ++i) {
            ImGui::PushID((int)i);          // номер ребёнка — часть идентификатора узла
            drawNode(*n.children[i]);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

void drawTreeTab() {
    if (!g.syntaxError.empty()) {
        textColored(ERROR_FG, g.syntaxError.c_str());
        textColored(MUTED, "Дерево появится, когда ошибка будет исправлена.");
        return;
    }
    if (!g.tree) return;
    ImGui::BeginChild("##treeView", ImVec2(0, 0));   // своя прокрутка для длинного дерева
    drawNode(*g.tree);
    ImGui::EndChild();
}

void drawResults(float width, float height) {
    ImGui::BeginChild("##results", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTabBar("##tabs")) {
        std::string title = "Лексемы  " + std::to_string(g.tokenCount) + "###tokens";
        if (ImGui::BeginTabItem(title.c_str())) {
            drawTable("##tokens", TAB_TOKENS, { { "№", 0 }, { "Лексема", 0.8f }, { "Класс", 1.4f },
                { "Описание", 1.6f }, { "Код", 0 }, { "Строка", 0 }, { "Позиция", 0 }, { "Значение", 0 } });
            ImGui::EndTabItem();
        }
        title = "Идентификаторы  " + std::to_string(g.rows[TAB_IDS].size()) + "###ids";
        if (ImGui::BeginTabItem(title.c_str())) {
            textColored(MUTED, g.idStats.c_str());
            drawTable("##ids", TAB_IDS, { { "№", 0 }, { "Имя", 1.5f }, { "Первое вхождение", 1.5f }, { "Вхождений", 0 } });
            ImGui::EndTabItem();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, g.syntaxError.empty() ? TEXT : ERROR_FG);
        const bool treeOpen = ImGui::BeginTabItem("Дерево###tree");
        ImGui::PopStyleColor();
        if (treeOpen) {
            drawTreeTab();
            ImGui::EndTabItem();
        }
        const bool hasErrors = !g.rows[TAB_ERRORS].empty();
        title = "Ошибки  " + std::to_string(g.rows[TAB_ERRORS].size()) + "###errors";
        ImGui::PushStyleColor(ImGuiCol_Text, hasErrors ? ERROR_FG : TEXT);
        const bool open = ImGui::BeginTabItem(title.c_str());
        ImGui::PopStyleColor();
        if (open) {
            drawTable("##errors", TAB_ERRORS, { { "№", 0 }, { "Строка", 0 }, { "Позиция", 0 },
                { "Фрагмент", 1.0f }, { "Сообщение", 3.0f } });
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

void drawStatus() {
    ImGui::AlignTextToFramePadding();
    if (!g.problem.empty())
        textColored(ERROR_FG, g.problem.c_str());
    else if (g.errorCount > 0)
        textColored(ERROR_FG, ("Найдено ошибок: " + std::to_string(g.errorCount) +
                               " из " + std::to_string(g.tokenCount) + " лексем").c_str());
    else
        textColored(MUTED, ("Разбор без ошибок, лексем: " + std::to_string(g.tokenCount)).c_str());

    const char* mode = g.live ? "UTF-8   анализ при вводе" : "UTF-8   анализ по F5";
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(mode).x - ImGui::GetStyle().WindowPadding.x);
    textColored(MUTED, mode);
}

} // namespace

// ============================================ вызывается из gui_platform.cpp

void guiInit(HWND hwnd, float scale) {
    g.hwnd = hwnd;
    g.scale = scale;
    loadFonts();
    setupStyle();
    setupEditor();

    // файл из командной строки, иначе test.mc из рабочей папки, иначе пустой
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool loaded = argv && argc > 1 && loadFile(argv[1]);
    if (argv) LocalFree(argv);
    wchar_t full[MAX_PATH];
    if (!loaded && GetFullPathNameW(L"test.mc", MAX_PATH, full, nullptr)) loaded = loadFile(full);
    if (!loaded) { updateTitle(); analyze(); }
}

void guiFrame() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) analyze();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openFile();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveFile(io.KeyShift);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);

    drawMenu();
    drawToolbar();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float height = avail.y - ImGui::GetFrameHeightWithSpacing();
    const float total = avail.x - 10.0f * g.scale;
    const float left = std::floor(total * g.split);
    drawEditor(left, height);
    drawSplitter(total, height);
    drawResults(total - left, height);
    drawStatus();

    ImGui::End();

    if (g.live && g.changedAt >= 0 && ImGui::GetTime() - g.changedAt > 0.3) analyze();
}

bool guiConfirmClose() {
    return confirmDiscard();
}
