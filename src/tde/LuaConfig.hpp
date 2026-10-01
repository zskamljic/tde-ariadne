#pragma once

#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <utility>

struct lua_State;

// Config files of TDE applications are Lua scripts returning a table.
namespace tde {

template <typename T> using Choices = std::span<const std::pair<QStringView, T>>;

// Reads fields of the table on top of the Lua stack. Only raw accesses are used, so a
// metatable in a config cannot raise a Lua error (a longjmp) through C++ frames. Values of
// the wrong type are reported as warnings and skipped.
class LuaTableReader {
public:
    LuaTableReader(lua_State* state, QStringList& warnings);

    std::optional<QString> string(const char* key);
    std::optional<bool> boolean(const char* key);
    std::optional<int> integer(const char* key, int min, int max);

    template <typename T> std::optional<T> choice(const char* key, Choices<T> choices)
    {
        const std::optional<QString> text = string(key);
        if (!text)
            return std::nullopt;
        QStringList names;
        for (const auto& [name, value] : choices) {
            if (text->compare(name, Qt::CaseInsensitive) == 0)
                return value;
            names << name.toString();
        }
        warn(key, QStringLiteral("unknown value \"%1\", expected one of: %2").arg(*text, names.join(u", ")));
        return std::nullopt;
    }

    // Runs `body` with the sub-table `key` on top of the stack, if there is one.
    void table(const char* key, const std::function<void()>& body);
    // Calls `f` for every string in the array part of the current table.
    void forEachString(const std::function<void(const QString&)>& f);
    // Runs `body` for every string key with a table value, with that table on top of the stack.
    void forEachTable(const std::function<void(const QString& key)>& body);
    // Calls `f` for every string key with a string value in the current table.
    void forEachStringPair(const std::function<void(const QString&, const QString&)>& f);

    void warn(QStringView key, const QString& message);
    void warn(const char* key, const QString& message);

private:
    int pushField(const char* key);

    lua_State* m_state;
    QStringList& m_warnings;
    QStringList m_path;
};

using LuaParser = std::function<void(LuaTableReader&)>;

// Runs the Lua file at `path` and hands the table it returns to `parse`. Returns the
// warnings reported while parsing, or the error that stopped the file from running.
std::expected<QStringList, QString> readLuaConfig(const QString& path, const LuaParser& parse);

// Like readLuaConfig(), but reports problems on stderr. A missing file is not a problem.
void loadLuaConfig(const QString& path, const LuaParser& parse);

// The name as it appears in a config file, for writing one.
template <typename T> QString choiceName(Choices<T> choices, T value)
{
    for (const auto& [name, candidate] : choices) {
        if (candidate == value)
            return name.toString();
    }
    return {};
}

// A quoted Lua string literal.
QString luaString(const QString& text);

// ~/.config/tde, the home of all TDE configuration.
QString configDirectory();

} // namespace tde
