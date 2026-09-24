// ============================================================================
//  gui.cpp — интерфейс лексического анализатора miniC
//  Dear ImGui + DirectX 11 + ImGuiColorTextEdit
//
//  Подключение — см. README.md рядом с этим файлом.
//
//  От лексера используется только: Lexer(const std::string&), nextToken(),
//  поля Token, codeName(), className(), TokenCode::EndOfFile / Error и
//  значения TokenClass. Подсветку синтаксиса в редакторе тоже делает лексер:
//  редактор отдаёт ему строку и красит то, что вернул nextToken.
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "TextEditor.h"

#include "Lexer.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

using PI = TextEditor::PaletteIndex;

// ================================================================== тема
// Цвета классов лексем — одни и те же в редакторе, в таблице и в легенде.
// Это главный визуальный приём интерфейса; всё остальное нарочно спокойное.

struct Theme {
    ImU32 bg, panel, raised, raisedHover, raisedActive, border;
    ImU32 text, muted, accent, accentText;
    ImU32 keyword, ident, constant, op, sep, error, comment, errorBg;
    ImU32 editorBg, lineNumber, currentLine, selection;
};

ImU32 hex(unsigned rgb, int alpha = 255) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

Theme darkTheme() {
    Theme t{};
    t.bg = hex(0x1E232A);          t.panel = hex(0x232932);
    t.raised = hex(0x2B323C);      t.raisedHover = hex(0x353D49);  t.raisedActive = hex(0x3E4755);
    t.border = hex(0x333B46);
    t.text = hex(0xDCE1E7);        t.muted = hex(0x8B95A3);
    t.accent = hex(0x4FA3E0);      t.accentText = hex(0x0E1A24);
    t.keyword = hex(0x6CB6FF);     t.ident = hex(0xDCE1E7);        t.constant = hex(0x7ED3B2);
    t.op = hex(0xC8A2F0);          t.sep = hex(0x97A1AE);          t.error = hex(0xFF7B72);
    t.comment = hex(0x6C7888);     t.errorBg = hex(0xFF7B72, 34);
    t.editorBg = hex(0x1A1F25);    t.lineNumber = hex(0x56606D);
    t.currentLine = hex(0xFFFFFF, 10); t.selection = hex(0x4FA3E0, 70);
    return t;
}

Theme lightTheme() {
    Theme t{};
    t.bg = hex(0xF3F4F6);          t.panel = hex(0xFFFFFF);
    t.raised = hex(0xEEF0F3);      t.raisedHover = hex(0xE3E7EC);  t.raisedActive = hex(0xD8DDE4);
    t.border = hex(0xD8DCE2);
    t.text = hex(0x1F2430);        t.muted = hex(0x6B7280);
    t.accent = hex(0x1F6FB8);      t.accentText = hex(0xFFFFFF);
    t.keyword = hex(0x0B5CAD);     t.ident = hex(0x1F2430);        t.constant = hex(0x0B7A5E);
    t.op = hex(0x7B3FB3);          t.sep = hex(0x5F6B7A);          t.error = hex(0xC0271D);
    t.comment = hex(0x8A94A3);     t.errorBg = hex(0xC0271D, 26);
    t.editorBg = hex(0xFFFFFF);    t.lineNumber = hex(0xA3ABB6);
    t.currentLine = hex(0x1F6FB8, 12); t.selection = hex(0x1F6FB8, 55);
    return t;
}

// ================================================================= данные

struct Row {
    std::vector<std::string> cells;
    std::string filterKey;     // по чему ищет фильтр
    TokenClass cls = TokenClass::Identificator;
    bool isError = false;
    int line = 0, col = 0, len = 0;   // позиция в байтах UTF-8, как у лексера
};

enum Tab { TAB_TOKENS, TAB_IDS, TAB_ERRORS, TAB_COUNT };

struct App {
    HWND hwnd = nullptr;
    float scale = 1.0f;
    bool dark = true;
    Theme theme = darkTheme();
    ImFont* uiFont = nullptr;
    ImFont* uiBold = nullptr;
    ImFont* codeFont = nullptr;

    TextEditor editor;
    bool live = true;
    bool dirty = false;
    double changedAt = -1.0;
    bool focusEditor = false;

    std::wstring filePath;
    std::string encodingNote = "UTF-8";

    std::vector<Row> rows[TAB_COUNT];
    int selected[TAB_COUNT] = { -1, -1, -1 };
    int tokenCount = 0, idCount = 0, errorCount = 0;
    std::string problem;
    ImGuiTextFilter filter;

    std::vector<std::string> lines;   // текст последнего анализа по строкам
    float split = 0.42f;              // доля ширины под редактор
};

App g;

const char* const SAMPLE_PROGRAM =
    "// тестовая программа miniC\n"
    "int fact(int n) {\n"
    "    if (n <= 1) { return 1; }   // база рекурсии\n"
    "    return n * fact(n - 1);\n"
    "}\n"
    "\n"
    "int main() {\n"
    "    int r = fact(5);\n"
    "    word mask = 1 << 3;\n"
    "    bool ok = r > 100 && mask != 0;\n"
    "    if (ok) { return 0; } else { return 1; }\n"
    "}\n";

// ============================================================== утилиты

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string fileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return toUtf8(p == std::wstring::npos ? path : path.substr(p + 1));
}

int utf8Length(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

// Лексер считает колонки в байтах, редактор — в видимых позициях
// (кириллическая буква — два байта, табуляция — до следующего стопа).
int visualColumn(const std::string& line, int byteCol, int tabSize) {
    int col = 0;
    size_t target = (size_t)std::max(byteCol - 1, 0);
    for (size_t i = 0; i < line.size() && i < target; ) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\t') { col = (col / tabSize + 1) * tabSize; ++i; }
        else { ++col; i += (size_t)utf8Length(c); }
    }
    return col;
}

std::string displayLexeme(const std::string& text) {
    if (text.size() == 1 && (unsigned char)text[0] >= 0x80) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "байт 0x%02X", (unsigned)(unsigned char)text[0]);
        return buf;
    }
    return text;
}

std::string positionText(const Token& t) {
    if (t.colStart == t.colEnd) return std::to_string(t.colStart);
    return std::to_string(t.colStart) + "–" + std::to_string(t.colEnd);
}

ImU32 classColor(TokenClass cls) {
    switch (cls) {
    case TokenClass::Keyword:   return g.theme.keyword;
    case TokenClass::Const:     return g.theme.constant;
    case TokenClass::OpSign:    return g.theme.op;
    case TokenClass::Separator: return g.theme.sep;
    case TokenClass::Mistake:   return g.theme.error;
    default:                    return g.theme.ident;
    }
}

// Индексы палитры редактора — просто ячейки для цветов. Классов лексем
// больше, чем «говорящих» имён в палитре, поэтому часть занята условно:
// CharLiteral — разделители, String — ошибки.
PI paletteFor(TokenClass cls) {
    switch (cls) {
    case TokenClass::Keyword:   return PI::Keyword;
    case TokenClass::Const:     return PI::Number;
    case TokenClass::OpSign:    return PI::Punctuation;
    case TokenClass::Separator: return PI::CharLiteral;
    case TokenClass::Mistake:   return PI::String;
    case TokenClass::Identificator: return PI::Identifier;
    default:                    return PI::Default;
    }
}

// ======================================================= подсветка лексером

// Редактор зовёт эту функцию для каждой строки: «найди первую лексему
// в [begin, end)». Ответ даёт лексер miniC — тот же, что строит таблицы.
bool tokenizeWithLexer(const char* begin, const char* end,
                       const char*& outBegin, const char*& outEnd, PI& color) {
    const std::string chunk(begin, end);
    outBegin = begin;
    outEnd = end;
    color = PI::Default;
    try {
        Lexer lexer(chunk);
        Token t = lexer.nextToken();
        if (t.code == TokenCode::EndOfFile || t.line != 1 || t.colStart < 1) return true;
        size_t offset = (size_t)(t.colStart - 1);
        if (offset >= chunk.size()) return true;
        size_t length = std::min(std::max<size_t>(1, t.text.size()), chunk.size() - offset);
        outBegin = begin + offset;
        outEnd = outBegin + length;
        color = paletteFor(t.cls);
    } catch (...) {
        // исключение лексера (например, stoi на огромном числе) — просто без цвета
    }
    return true;
}

TextEditor::LanguageDefinition miniCLanguage() {
    TextEditor::LanguageDefinition def;
    def.mName = "miniC";
    def.mSingleLineComment = "//";
    def.mCommentStart = "\x01\x01";   // блочных комментариев в miniC нет —
    def.mCommentEnd = "\x02\x02";     // ставим последовательности, которых не бывает
    def.mPreprocChar = '\x01';        // препроцессора тоже нет
    def.mCaseSensitive = true;
    def.mAutoIndentation = true;
    def.mTokenize = tokenizeWithLexer;
    return def;
}

// ============================================================ применение темы

void applyTheme() {
    g.theme = g.dark ? darkTheme() : lightTheme();
    const Theme& t = g.theme;

    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    if (g.dark) ImGui::StyleColorsDark(&s); else ImGui::StyleColorsLight(&s);

    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(10, 6);
    s.CellPadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(6, 6);
    s.ScrollbarSize = 12;
    s.WindowRounding = 0;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 6;
    s.TabRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.TabBarOverlineSize = 2;
    s.SeparatorTextBorderSize = 1;
    s.ScaleAllSizes(g.scale);

    auto c = [](ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); };
    ImVec4* col = s.Colors;
    col[ImGuiCol_Text] = c(t.text);
    col[ImGuiCol_TextDisabled] = c(t.muted);
    col[ImGuiCol_WindowBg] = c(t.bg);
    col[ImGuiCol_ChildBg] = c(t.panel);
    col[ImGuiCol_PopupBg] = c(t.panel);
    col[ImGuiCol_MenuBarBg] = c(t.bg);
    col[ImGuiCol_Border] = c(t.border);
    col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg] = c(t.raised);
    col[ImGuiCol_FrameBgHovered] = c(t.raisedHover);
    col[ImGuiCol_FrameBgActive] = c(t.raisedActive);
    col[ImGuiCol_Button] = c(t.raised);
    col[ImGuiCol_ButtonHovered] = c(t.raisedHover);
    col[ImGuiCol_ButtonActive] = c(t.raisedActive);
    col[ImGuiCol_Header] = c(t.selection);
    col[ImGuiCol_HeaderHovered] = c(t.raisedHover);
    col[ImGuiCol_HeaderActive] = c(t.raisedActive);
    col[ImGuiCol_CheckMark] = c(t.accent);
    col[ImGuiCol_SliderGrab] = c(t.accent);
    col[ImGuiCol_Separator] = c(t.border);
    col[ImGuiCol_SeparatorHovered] = c(t.accent);
    col[ImGuiCol_SeparatorActive] = c(t.accent);
    col[ImGuiCol_Tab] = c(t.panel);
    col[ImGuiCol_TabHovered] = c(t.raisedHover);
    col[ImGuiCol_TabSelected] = c(t.raised);
    col[ImGuiCol_TabSelectedOverline] = c(t.accent);
    col[ImGuiCol_TabDimmed] = c(t.panel);
    col[ImGuiCol_TabDimmedSelected] = c(t.raised);
    col[ImGuiCol_TableHeaderBg] = c(t.raised);
    col[ImGuiCol_TableBorderStrong] = c(t.border);
    col[ImGuiCol_TableBorderLight] = c(t.border);
    col[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TableRowBgAlt] = c(g.dark ? hex(0xFFFFFF, 6) : hex(0x000000, 8));
    col[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ScrollbarGrab] = c(t.raisedActive);
    col[ImGuiCol_ScrollbarGrabHovered] = c(t.muted);
    col[ImGuiCol_ScrollbarGrabActive] = c(t.muted);
    col[ImGuiCol_TextSelectedBg] = c(t.selection);
    col[ImGuiCol_NavCursor] = c(t.accent);

    TextEditor::Palette p = g.dark ? TextEditor::GetDarkPalette() : TextEditor::GetLightPalette();
    p[(int)PI::Default] = t.text;
    p[(int)PI::Keyword] = t.keyword;
    p[(int)PI::Number] = t.constant;
    p[(int)PI::Punctuation] = t.op;
    p[(int)PI::CharLiteral] = t.sep;
    p[(int)PI::String] = t.error;
    p[(int)PI::Identifier] = t.ident;
    p[(int)PI::KnownIdentifier] = t.ident;
    p[(int)PI::Comment] = t.comment;
    p[(int)PI::MultiLineComment] = t.comment;
    p[(int)PI::Background] = t.editorBg;
    p[(int)PI::Cursor] = t.text;
    p[(int)PI::Selection] = t.selection;
    p[(int)PI::ErrorMarker] = hex(g.dark ? 0xFF7B72 : 0xC0271D, g.dark ? 30 : 24);
    p[(int)PI::LineNumber] = t.lineNumber;
    p[(int)PI::CurrentLineFill] = t.currentLine;
    p[(int)PI::CurrentLineFillInactive] = t.currentLine;
    p[(int)PI::CurrentLineEdge] = hex(0, 0);
    g.editor.SetPalette(p);

    BOOL darkTitle = g.dark ? TRUE : FALSE;   // тёмный заголовок окна Windows 10/11
    DwmSetWindowAttribute(g.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkTitle, sizeof(darkTitle));
}

// ================================================================= анализ

void selectInEditor(int line, int byteCol, int byteLen) {
    if (line < 1 || line > (int)g.lines.size()) return;
    const std::string& text = g.lines[line - 1];
    int tab = g.editor.GetTabSize();
    int a = visualColumn(text, byteCol, tab);
    int b = visualColumn(text, byteCol + std::max(byteLen, 1), tab);
    g.editor.SetCursorPosition(TextEditor::Coordinates(line - 1, b));
    g.editor.SetSelection(TextEditor::Coordinates(line - 1, a), TextEditor::Coordinates(line - 1, b));
}

void analyze() {
    g.changedAt = -1.0;
    const std::string source = g.editor.GetText();

    g.lines.clear();
    size_t start = 0;
    for (size_t i = 0; i <= source.size(); ++i) {
        if (i == source.size() || source[i] == '\n') {
            g.lines.push_back(source.substr(start, i - start));
            start = i + 1;
        }
    }

    // 1. Прогон лексера с защитой от зависания и исключений
    std::vector<Token> tokens;
    g.problem.clear();
    int problemLine = 0, problemCol = 0;
    try {
        Lexer lexer(source);
        int prevLine = -1, prevCol = -1;
        while (true) {
            Token t = lexer.nextToken();
            if (t.code == TokenCode::EndOfFile) break;
            if (t.line == prevLine && t.colStart == prevCol) {
                g.problem = "Лексер не продвигается: nextToken дважды вернул лексему с одной позиции. "
                            "Проверьте, что каждая ветка забирает хотя бы один символ.";
                problemLine = t.line;
                problemCol = t.colStart;
                break;
            }
            prevLine = t.line;
            prevCol = t.colStart;
            tokens.push_back(t);
            if (tokens.size() >= 200000) {
                g.problem = "Разбор остановлен на 200000 лексемах.";
                break;
            }
        }
    } catch (const std::exception& e) {
        g.problem = std::string("Лексер завершился с исключением: ") + e.what();
        if (!tokens.empty()) {
            problemLine = tokens.back().line;
            problemCol = tokens.back().colEnd + 1;
        }
    }

    // 2. Раскладка по таблицам
    for (auto& r : g.rows) r.clear();
    for (int& s : g.selected) s = -1;
    TextEditor::ErrorMarkers markers;

    // Таблица идентификаторов пока собирается здесь, из потока лексем.
    // По заданию её строит лексер методом цепочек (шаг 9) — тогда этот блок
    // заменяется чтением из лексера.
    struct IdInfo { std::string name; int line; int col; int count; };
    std::vector<IdInfo> ids;
    std::unordered_map<std::string, int> idIndex;

    int errors = 0;
    for (size_t n = 0; n < tokens.size(); ++n) {
        const Token& t = tokens[n];

        std::string value;
        if (t.cls == TokenClass::Identificator) {
            auto it = idIndex.find(t.text);
            int idx;
            if (it == idIndex.end()) {
                idx = (int)ids.size();
                idIndex.emplace(t.text, idx);
                ids.push_back({ t.text, t.line, t.colStart, 0 });
            } else {
                idx = it->second;
            }
            ids[idx].count++;
            value = "№ " + std::to_string(idx + 1);
        } else if (t.cls == TokenClass::Const) {
            value = std::to_string(t.value);
        }

        Row r;
        r.cells = {
            std::to_string(n + 1), displayLexeme(t.text), className(t.cls), codeName(t.code),
            std::to_string((int)t.code), std::to_string(t.line), positionText(t), value,
        };
        r.filterKey = t.text + " " + r.cells[2] + " " + r.cells[3];
        r.cls = t.cls;
        r.isError = (t.code == TokenCode::Error);
        r.line = t.line;
        r.col = t.colStart;
        r.len = (int)std::max<size_t>(1, t.text.size());
        g.rows[TAB_TOKENS].push_back(r);

        if (r.isError) {
            ++errors;
            Row e;
            e.cells = { std::to_string(errors), std::to_string(t.line), positionText(t),
                        displayLexeme(t.text), codeName(t.code) };
            e.cls = TokenClass::Mistake;
            e.isError = true;
            e.line = r.line;
            e.col = r.col;
            e.len = r.len;
            g.rows[TAB_ERRORS].push_back(e);

            std::string& m = markers[t.line];
            if (!m.empty()) m += "\n";
            m += codeName(t.code) + ": " + displayLexeme(t.text) + ", позиция " + positionText(t);
        }
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        Row r;
        r.cells = { std::to_string(i + 1), ids[i].name,
                    "стр " + std::to_string(ids[i].line) + ", поз " + std::to_string(ids[i].col),
                    std::to_string(ids[i].count) };
        r.line = ids[i].line;
        r.col = ids[i].col;
        r.len = (int)ids[i].name.size();
        g.rows[TAB_IDS].push_back(r);
    }

    if (!g.problem.empty()) {
        Row e;
        e.cells = { std::to_string(g.rows[TAB_ERRORS].size() + 1),
                    problemLine > 0 ? std::to_string(problemLine) : "",
                    problemCol > 0 ? std::to_string(problemCol) : "", "", g.problem };
        e.cls = TokenClass::Mistake;
        e.isError = true;
        e.line = problemLine;
        e.col = problemCol;
        e.len = 1;
        g.rows[TAB_ERRORS].push_back(e);
        if (problemLine > 0) {
            std::string& m = markers[problemLine];
            if (!m.empty()) m += "\n";
            m += g.problem;
        }
    }

    g.tokenCount = (int)tokens.size();
    g.idCount = (int)ids.size();
    g.errorCount = errors;
    g.editor.SetErrorMarkers(markers);
}

// ================================================================== файлы

void setWindowTitle() {
    std::string name = g.filePath.empty() ? "Без имени" : fileNameOf(g.filePath);
    std::wstring title = toWide(name + (g.dirty ? " (изменён)" : "") + " — miniC");
    SetWindowTextW(g.hwnd, title.c_str());
}

bool loadFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        std::wstring msg = L"Не удалось открыть файл:\n" + path;
        MessageBoxW(g.hwnd, msg.c_str(), L"miniC", MB_ICONWARNING);
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    std::string bytes((size_t)size.QuadPart, '\0');
    DWORD got = 0;
    BOOL ok = bytes.empty() || ReadFile(f, &bytes[0], (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(f);
    if (!ok) {
        MessageBoxW(g.hwnd, L"Ошибка чтения файла.", L"miniC", MB_ICONWARNING);
        return false;
    }
    bytes.resize(got);

    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);

    bool utf8 = bytes.empty() ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0) > 0;
    if (utf8) {
        g.encodingNote = "UTF-8";
    } else {
        int n = MultiByteToWideChar(1251, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(1251, 0, bytes.data(), (int)bytes.size(), &w[0], n);
        bytes = toUtf8(w);
        g.encodingNote = "Windows-1251, сохранится в UTF-8";
    }

    std::string text;
    text.reserve(bytes.size());
    for (char c : bytes)
        if (c != '\r') text += c;

    g.editor.SetText(text);
    g.filePath = path;
    g.dirty = false;
    setWindowTitle();
    analyze();
    return true;
}

bool saveFile(bool askPath) {
    std::wstring path = g.filePath;
    if (askPath || path.empty()) {
        wchar_t buf[MAX_PATH] = L"";
        if (!path.empty()) lstrcpynW(buf, path.c_str(), MAX_PATH);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g.hwnd;
        ofn.lpstrFilter = L"Программы miniC (*.mc)\0*.mc\0Все файлы (*.*)\0*.*\0";
        ofn.lpstrFile = buf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"mc";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return false;
        path = buf;
    }

    std::string text = g.editor.GetText();
    std::string bytes;                   // UTF-8 без BOM, переводы строк Windows
    bytes.reserve(text.size() + text.size() / 20);
    for (char c : text) {
        if (c == '\n') bytes += "\r\n";
        else bytes += c;
    }

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        std::wstring msg = L"Не удалось сохранить файл:\n" + path;
        MessageBoxW(g.hwnd, msg.c_str(), L"miniC", MB_ICONWARNING);
        return false;
    }
    DWORD written = 0;
    BOOL ok = bytes.empty() || WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(f);
    if (!ok || written != bytes.size()) {
        MessageBoxW(g.hwnd, L"Файл записан не полностью.", L"miniC", MB_ICONWARNING);
        return false;
    }

    g.filePath = path;
    g.dirty = false;
    g.encodingNote = "UTF-8";
    setWindowTitle();
    return true;
}

bool confirmDiscard() {
    if (!g.dirty) return true;
    int r = MessageBoxW(g.hwnd, L"Сохранить изменения перед тем, как продолжить?",
                        L"miniC", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return saveFile(false);
    return true;
}

void openWithDialog() {
    if (!confirmDiscard()) return;
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Программы miniC (*.mc)\0*.mc\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) loadFile(buf);
}

// ============================================================= отрисовка UI

void textColored(ImU32 color, const char* s) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
}

bool accentButton(const char* label) {
    const Theme& t = g.theme;
    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (t.accent & 0x00FFFFFF) | 0xE6000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (t.accent & 0x00FFFFFF) | 0xCC000000);
    ImGui::PushStyleColor(ImGuiCol_Text, t.accentText);
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return pressed;
}

void legendItem(ImU32 color, const char* label) {
    float r = ImGui::GetFontSize() * 0.28f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, color);
    ImGui::Dummy(ImVec2(r * 2, h));
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    textColored(g.theme.muted, label);
}

void drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("Файл")) {
        if (ImGui::MenuItem("Открыть…", "Ctrl+O")) openWithDialog();
        if (ImGui::MenuItem("Сохранить", "Ctrl+S")) saveFile(false);
        if (ImGui::MenuItem("Сохранить как…", "Ctrl+Shift+S")) saveFile(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Выход")) PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Анализ")) {
        if (ImGui::MenuItem("Запустить", "F5")) analyze();
        ImGui::MenuItem("Анализ при вводе", nullptr, &g.live);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Вид")) {
        if (ImGui::MenuItem("Тёмная тема", nullptr, g.dark)) {
            g.dark = !g.dark;
            applyTheme();
        }
        bool ws = g.editor.IsShowingWhitespaces();
        if (ImGui::MenuItem("Показывать пробелы", nullptr, ws)) g.editor.SetShowWhitespaces(!ws);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void drawToolbar() {
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::SetCursorPosX(s.WindowPadding.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s.ItemSpacing.y * 0.5f);

    if (ImGui::Button("Открыть")) openWithDialog();
    ImGui::SameLine();
    if (ImGui::Button("Сохранить")) saveFile(false);
    ImGui::SameLine();
    if (accentButton("Анализ  F5")) analyze();
    ImGui::SameLine(0, s.ItemSpacing.x * 2);
    ImGui::AlignTextToFramePadding();
    ImGui::Checkbox("При вводе", &g.live);

    // справа: имя файла и отметка об изменениях
    std::string name = g.filePath.empty() ? "Без имени" : fileNameOf(g.filePath);
    const char* mark = g.dirty ? "изменён" : "";
    float nameW = ImGui::CalcTextSize(name.c_str()).x;
    float markW = g.dirty ? ImGui::CalcTextSize(mark).x + s.FramePadding.x * 2 + s.ItemSpacing.x : 0.0f;
    float right = ImGui::GetWindowWidth() - s.WindowPadding.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - nameW - markW));
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(g.uiBold);
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopFont();
    if (!g.filePath.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toUtf8(g.filePath).c_str());
    if (g.dirty) {
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 sz = ImGui::CalcTextSize(mark);
        ImVec2 pad = s.FramePadding;
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + sz.x + pad.x * 2, p.y + sz.y + pad.y * 2), g.theme.raised, s.FrameRounding);
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad.x, p.y + pad.y));
        textColored(g.theme.muted, mark);
    }
}

void drawEditorPane(float width, float height) {
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##editorPane", ImVec2(width, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // шапка панели: название и позиция каретки
    ImGui::SetCursorPos(ImVec2(s.WindowPadding.x, s.FramePadding.y + 2));
    ImGui::PushFont(g.uiBold);
    ImGui::TextUnformatted("Программа");
    ImGui::PopFont();
    auto cur = g.editor.GetCursorPosition();
    std::string pos = "Стр " + std::to_string(cur.mLine + 1) + ", кол " + std::to_string(cur.mColumn + 1);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(pos.c_str()).x - s.WindowPadding.x);
    textColored(g.theme.muted, pos.c_str());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);

    ImGui::PushFont(g.codeFont);
    if (g.focusEditor) {
        ImGui::SetNextWindowFocus();
        g.focusEditor = false;
    }
    g.editor.Render("##code", ImVec2(0, 0), false);
    ImGui::PopFont();

    if (g.editor.IsTextChanged()) {
        if (!g.dirty) {
            g.dirty = true;
            setWindowTitle();
        }
        g.changedAt = ImGui::GetTime();
    }
    ImGui::EndChild();
}

// Общая часть трёх таблиц: строки, цвет по классу, клик — переход к месту.
void drawTable(const char* id, Tab tab, const std::vector<const char*>& cols,
               const std::vector<float>& weights, bool useFilter) {
    std::vector<Row>& rows = g.rows[tab];

    std::vector<int> visible;
    visible.reserve(rows.size());
    for (int i = 0; i < (int)rows.size(); ++i)
        if (!useFilter || g.filter.PassFilter(rows[i].filterKey.c_str())) visible.push_back(i);

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable(id, (int)cols.size(), flags, ImVec2(0, 0))) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    for (size_t c = 0; c < cols.size(); ++c) {
        if (weights[c] > 0.0f)
            ImGui::TableSetupColumn(cols[c], ImGuiTableColumnFlags_WidthStretch, weights[c]);
        else   // 0 — узкая колонка: ширина по заголовку и содержимому
            ImGui::TableSetupColumn(cols[c], ImGuiTableColumnFlags_WidthFixed);
    }
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin((int)visible.size());
    while (clipper.Step()) {
        for (int v = clipper.DisplayStart; v < clipper.DisplayEnd; ++v) {
            const int i = visible[v];
            const Row& r = rows[i];

            ImGui::TableNextRow();
            if (r.isError) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, g.theme.errorBg);
            ImGui::PushStyleColor(ImGuiCol_Text, tab == TAB_IDS ? g.theme.text : classColor(r.cls));

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            const bool selected = (g.selected[tab] == i);
            if (ImGui::Selectable(r.cells[0].c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                g.selected[tab] = i;
                if (r.line > 0) selectInEditor(r.line, r.col, r.len);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) g.focusEditor = true;
            }
            ImGui::PopID();

            for (int c = 1; c < (int)r.cells.size() && c < (int)cols.size(); ++c) {
                ImGui::TableSetColumnIndex(c);
                ImGui::TextUnformatted(r.cells[c].c_str());
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndTable();
}

void drawLegend() {
    struct Item { ImU32 color; const char* label; };
    const Item items[] = {
        { g.theme.keyword, "ключевые слова" }, { g.theme.ident, "идентификаторы" },
        { g.theme.constant, "константы" },     { g.theme.op, "операции" },
        { g.theme.sep, "разделители" },        { g.theme.error, "ошибки" },
    };
    const ImGuiStyle& s = ImGui::GetStyle();
    const float dot = ImGui::GetFontSize() * 0.56f;
    for (const Item& it : items) {
        float w = dot + s.ItemInnerSpacing.x + ImGui::CalcTextSize(it.label).x;
        ImGui::SameLine(0, s.ItemSpacing.x * 2);
        if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
        legendItem(it.color, it.label);
    }
}

void drawEmptyState(const char* title, const char* hint) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.35f);
    float w1 = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((avail.x - w1) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushFont(g.uiBold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    float w2 = ImGui::CalcTextSize(hint).x;
    ImGui::SetCursorPosX((avail.x - w2) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    textColored(g.theme.muted, hint);
}

void drawResultsPane(float width, float height) {
    ImGui::BeginChild("##results", ImVec2(width, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::BeginTabBar("##tabs")) {
        const std::string tTokens = "Лексемы  " + std::to_string(g.tokenCount) + "###tokens";
        const std::string tIds = "Идентификаторы  " + std::to_string(g.idCount) + "###ids";
        const std::string tErr = "Ошибки  " + std::to_string(g.rows[TAB_ERRORS].size()) + "###errors";

        if (ImGui::BeginTabItem(tTokens.c_str())) {
            ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.45f, 280.0f * g.scale));
            if (ImGui::InputTextWithHint("##filter", "Фильтр: лексема или класс",
                                         g.filter.InputBuf, IM_ARRAYSIZE(g.filter.InputBuf)))
                g.filter.Build();
            drawLegend();
            drawTable("##tokensTable", TAB_TOKENS,
                      { "№", "Лексема", "Класс", "Описание", "Код", "Строка", "Позиция", "Значение" },
                      { 0.0f, 0.8f, 1.4f, 1.6f, 0.0f, 0.0f, 0.0f, 0.0f }, true);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(tIds.c_str())) {
            if (g.rows[TAB_IDS].empty())
                drawEmptyState("Идентификаторов нет", "Они появятся, как только в программе будут имена");
            else
                drawTable("##idsTable", TAB_IDS, { "№", "Имя", "Первое вхождение", "Вхождений" },
                          { 0.0f, 1.5f, 1.5f, 0.0f }, false);
            ImGui::EndTabItem();
        }

        const bool hasErrors = !g.rows[TAB_ERRORS].empty();
        if (hasErrors) ImGui::PushStyleColor(ImGuiCol_Text, g.theme.error);
        const bool errOpen = ImGui::BeginTabItem(tErr.c_str());
        if (hasErrors) ImGui::PopStyleColor();
        if (errOpen) {
            if (!hasErrors)
                drawEmptyState("Ошибок нет", "Лексический анализ прошёл без замечаний");
            else
                drawTable("##errTable", TAB_ERRORS, { "№", "Строка", "Позиция", "Фрагмент", "Сообщение" },
                          { 0.0f, 0.0f, 0.0f, 1.0f, 3.0f }, false);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

void drawSplitter(float total, float height) {
    const float w = 10.0f * g.scale;
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##split", ImVec2(w, height));
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && total > 1.0f)
        g.split = std::min(0.75f, std::max(0.25f, g.split + ImGui::GetIO().MouseDelta.x / total));

    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f, half = 18.0f * g.scale;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(cx, cy - half), ImVec2(cx, cy + half),
                                        hot ? g.theme.accent : g.theme.border, 3.0f * g.scale);
    ImGui::SameLine(0, 0);
}

void drawStatusBar() {
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::SetCursorPosX(s.WindowPadding.x);
    ImGui::AlignTextToFramePadding();

    ImU32 color = g.theme.muted;
    std::string text;
    if (!g.problem.empty()) {
        color = g.theme.error;
        text = g.problem;
    } else if (g.errorCount == 0) {
        text = "Разбор без ошибок, лексем: " + std::to_string(g.tokenCount);
    } else {
        color = g.theme.error;
        text = "Найдено ошибок: " + std::to_string(g.errorCount) + " из " + std::to_string(g.tokenCount) + " лексем";
    }
    textColored(color, text.c_str());

    std::string right = g.encodingNote + "   " + (g.live ? "анализ при вводе" : "анализ по F5");
    float w = ImGui::CalcTextSize(right.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - w - s.WindowPadding.x));
    textColored(g.theme.muted, right.c_str());
}

void handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) analyze();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openWithDialog();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveFile(io.KeyShift);
}

void drawUI() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    drawMenuBar();
    drawToolbar();

    const ImGuiStyle& s = ImGui::GetStyle();
    const float statusH = ImGui::GetFrameHeight() + s.ItemSpacing.y;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float height = std::max(50.0f, avail.y - statusH);
    const float splitW = 10.0f * g.scale;
    const float total = std::max(1.0f, avail.x - splitW);
    const float leftW = std::floor(total * g.split);

    drawEditorPane(leftW, height);
    drawSplitter(total, height);
    drawResultsPane(total - leftW, height);
    drawStatusBar();

    ImGui::End();
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    builder.AddChar(0x2013);   // –
    builder.AddChar(0x2014);   // —
    builder.AddChar(0x2026);   // …
    builder.AddChar(0x2116);   // №
    builder.BuildRanges(&ranges);

    wchar_t windir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::wstring dir = std::wstring(windir) + L"\\Fonts\\";

    auto tryLoad = [&](std::initializer_list<const wchar_t*> names, float size) -> ImFont* {
        for (const wchar_t* n : names) {
            std::wstring path = dir + n;
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return io.Fonts->AddFontFromFileTTF(toUtf8(path).c_str(), size, nullptr, ranges.Data);
        }
        return nullptr;
    };

    const float ui = 16.0f * g.scale;
    const float code = 16.0f * g.scale;
    g.uiFont = tryLoad({ L"segoeui.ttf" }, ui);
    if (!g.uiFont) g.uiFont = io.Fonts->AddFontDefault();
    g.uiBold = tryLoad({ L"seguisb.ttf", L"segoeuib.ttf" }, ui);
    if (!g.uiBold) g.uiBold = g.uiFont;
    g.codeFont = tryLoad({ L"CascadiaMono.ttf", L"consola.ttf" }, code);
    if (!g.codeFont) g.codeFont = g.uiFont;
}

// ================================================================ DirectX 11

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gTarget = nullptr;
bool gOccluded = false;
UINT gResizeW = 0, gResizeH = 0;

void createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    gSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        gDevice->CreateRenderTargetView(back, nullptr, &gTarget);
        back->Release();
    }
}

void cleanupRenderTarget() {
    if (gTarget) { gTarget->Release(); gTarget = nullptr; }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &gSwapChain, &gDevice, &got, &gContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // нет подходящей видеокарты — программный рендер
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &gSwapChain, &gDevice, &got, &gContext);
    if (hr != S_OK) return false;
    createRenderTarget();
    return true;
}

void cleanupDevice() {
    cleanupRenderTarget();
    if (gSwapChain) { gSwapChain->Release(); gSwapChain = nullptr; }
    if (gContext) { gContext->Release(); gContext = nullptr; }
    if (gDevice) { gDevice->Release(); gDevice = nullptr; }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;

    switch (m) {
    case WM_SIZE:
        if (w == SIZE_MINIMIZED) return 0;
        gResizeW = (UINT)LOWORD(l);
        gResizeH = (UINT)HIWORD(l);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(l);
        mmi->ptMinTrackSize.x = (LONG)(900 * g.scale);
        mmi->ptMinTrackSize.y = (LONG)(560 * g.scale);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<RECT*>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES: {                       // файл можно перетащить в окно
        HDROP drop = reinterpret_cast<HDROP>(w);
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(drop, 0, path, MAX_PATH) && confirmDiscard()) loadFile(path);
        DragFinish(drop);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) return 0;   // Alt не уводит фокус в системное меню
        break;
    case WM_CLOSE:
        if (confirmDiscard()) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

// =============================================================== точка входа

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    g.scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MiniCStudio";
    RegisterClassExW(&wc);

    g.hwnd = CreateWindowW(wc.lpszClassName, L"miniC", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           (int)(1280 * g.scale), (int)(800 * g.scale), nullptr, nullptr, hinst, nullptr);
    if (!g.hwnd || !createDevice(g.hwnd)) {
        cleanupDevice();
        MessageBoxW(nullptr, L"Не удалось инициализировать DirectX 11.", L"miniC", MB_ICONERROR);
        return 1;
    }
    DragAcceptFiles(g.hwnd, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // не создавать imgui.ini рядом с программой
    ImGui_ImplWin32_Init(g.hwnd);
    ImGui_ImplDX11_Init(gDevice, gContext);
    loadFonts();

    g.editor.SetLanguageDefinition(miniCLanguage());
    g.editor.SetTabSize(4);
    g.editor.SetShowWhitespaces(false);
    applyTheme();

    ShowWindow(g.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g.hwnd);

    // Что открыть: файл из командной строки, иначе test.mc рядом, иначе пример
    bool loaded = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) loaded = loadFile(argv[1]);
    if (argv) LocalFree(argv);
    if (!loaded && GetFileAttributesW(L"test.mc") != INVALID_FILE_ATTRIBUTES) {
        wchar_t full[MAX_PATH];
        if (GetFullPathNameW(L"test.mc", MAX_PATH, full, nullptr)) loaded = loadFile(full);
    }
    if (!loaded) {
        g.editor.SetText(SAMPLE_PROGRAM);
        setWindowTitle();
        analyze();
    }

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (gOccluded && gSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        gOccluded = false;

        if (gResizeW != 0 && gResizeH != 0) {
            cleanupRenderTarget();
            gSwapChain->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
            gResizeW = gResizeH = 0;
            createRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        handleShortcuts();
        drawUI();
        ImGui::Render();

        if (g.live && g.changedAt >= 0.0 && ImGui::GetTime() - g.changedAt > 0.3)
            analyze();

        const ImVec4 bg = ImGui::ColorConvertU32ToFloat4(g.theme.bg);
        const float clear[4] = { bg.x, bg.y, bg.z, 1.0f };
        gContext->OMSetRenderTargets(1, &gTarget, nullptr);
        gContext->ClearRenderTargetView(gTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = gSwapChain->Present(1, 0);
        gOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(g.hwnd);
    UnregisterClassW(wc.lpszClassName, hinst);
    return 0;
}
