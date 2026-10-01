#include "LuaConfig.hpp"

#include <QFile>
#include <QStandardPaths>

#include <cstdio>
#include <memory>
#include <print>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace Qt::StringLiterals;

namespace tde {

LuaTableReader::LuaTableReader(lua_State* state, QStringList& warnings)
    : m_state(state)
    , m_warnings(warnings)
{
}

std::optional<QString> LuaTableReader::string(const char* key)
{
    std::optional<QString> result;
    const int type = pushField(key);
    if (type == LUA_TSTRING)
        result = QString::fromUtf8(lua_tostring(m_state, -1));
    else if (type != LUA_TNIL)
        warn(key, u"expected a string"_s);
    lua_pop(m_state, 1);
    return result;
}

std::optional<bool> LuaTableReader::boolean(const char* key)
{
    std::optional<bool> result;
    const int type = pushField(key);
    if (type == LUA_TBOOLEAN)
        result = lua_toboolean(m_state, -1) != 0;
    else if (type != LUA_TNIL)
        warn(key, u"expected true or false"_s);
    lua_pop(m_state, 1);
    return result;
}

std::optional<int> LuaTableReader::integer(const char* key, int min, int max)
{
    std::optional<int> result;
    const int type = pushField(key);
    int isInteger = 0;
    const lua_Integer value = type == LUA_TNUMBER ? lua_tointegerx(m_state, -1, &isInteger) : 0;
    if (isInteger && value >= min && value <= max)
        result = static_cast<int>(value);
    else if (type != LUA_TNIL)
        warn(key, u"expected a whole number between %1 and %2"_s.arg(min).arg(max));
    lua_pop(m_state, 1);
    return result;
}

void LuaTableReader::table(const char* key, const std::function<void()>& body)
{
    const int type = pushField(key);
    if (type == LUA_TTABLE) {
        m_path << QString::fromUtf8(key);
        body();
        m_path.removeLast();
    } else if (type != LUA_TNIL) {
        warn(key, u"expected a table"_s);
    }
    lua_pop(m_state, 1);
}

void LuaTableReader::forEachString(const std::function<void(const QString&)>& f)
{
    const auto length = static_cast<lua_Integer>(lua_rawlen(m_state, -1));
    for (lua_Integer i = 1; i <= length; ++i) {
        if (lua_rawgeti(m_state, -1, i) == LUA_TSTRING)
            f(QString::fromUtf8(lua_tostring(m_state, -1)));
        else
            warn(QString::number(i), u"expected a string"_s);
        lua_pop(m_state, 1);
    }
}

void LuaTableReader::forEachTable(const std::function<void(const QString& key)>& body)
{
    lua_pushnil(m_state);
    while (lua_next(m_state, -2) != 0) {
        if (lua_type(m_state, -2) == LUA_TSTRING && lua_type(m_state, -1) == LUA_TTABLE) {
            const QString key = QString::fromUtf8(lua_tostring(m_state, -2));
            m_path << key;
            body(key);
            m_path.removeLast();
        } else {
            warn(QStringView(u"?"), u"expected tables under string keys"_s);
        }
        lua_pop(m_state, 1);
    }
}

void LuaTableReader::forEachStringPair(const std::function<void(const QString&, const QString&)>& f)
{
    lua_pushnil(m_state);
    while (lua_next(m_state, -2) != 0) {
        if (lua_type(m_state, -2) == LUA_TSTRING && lua_type(m_state, -1) == LUA_TSTRING)
            f(QString::fromUtf8(lua_tostring(m_state, -2)), QString::fromUtf8(lua_tostring(m_state, -1)));
        else
            warn(QStringView(u"?"), u"expected string keys and string values"_s);
        lua_pop(m_state, 1);
    }
}

void LuaTableReader::warn(QStringView key, const QString& message)
{
    QStringList path = m_path;
    path << key.toString();
    m_warnings << u"%1: %2"_s.arg(path.join(u'.'), message);
}

void LuaTableReader::warn(const char* key, const QString& message)
{
    warn(QString::fromUtf8(key), message);
}

int LuaTableReader::pushField(const char* key)
{
    lua_pushstring(m_state, key);
    return lua_rawget(m_state, -2);
}

std::expected<QStringList, QString> readLuaConfig(const QString& path, const LuaParser& parse)
{
    const std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    if (!state)
        return std::unexpected(u"could not create a Lua state"_s);

    lua_State* L = state.get();
    luaL_openlibs(L);
    if (luaL_loadfile(L, QFile::encodeName(path).constData()) != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK)
        return std::unexpected(QString::fromUtf8(lua_tostring(L, -1)));
    if (!lua_istable(L, -1))
        return std::unexpected(u"%1: the file must return a table"_s.arg(path));

    QStringList warnings;
    LuaTableReader reader(L, warnings);
    parse(reader);
    return warnings;
}

void loadLuaConfig(const QString& path, const LuaParser& parse)
{
    if (!QFile::exists(path))
        return;

    const auto warnings = readLuaConfig(path, parse);
    if (!warnings) {
        std::println(stderr, "{}", warnings.error().toStdString());
        return;
    }
    for (const QString& warning : *warnings)
        std::println(stderr, "{}: {}", path.toStdString(), warning.toStdString());
}

QString luaString(const QString& text)
{
    QString escaped = text;
    escaped.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s).replace(u'\n', u"\\n"_s);
    return u'"' + escaped + u'"';
}

QString configDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/tde"_s;
}

} // namespace tde
