#ifndef LMMS_PIXMAP_LOADER_H
#define LMMS_PIXMAP_LOADER_H

#include <string>
#include <QPixmap>

#ifdef PLUGIN_NAME
#include "LmmsCommonMacros.h"
#endif


namespace lmms
{

class PixmapLoader
{
public:
	PixmapLoader() = default;

	explicit PixmapLoader(std::string name, const char* const* xpm = nullptr) :
		m_name{std::move(name)},
		m_xpm{xpm}
	{ }

	virtual ~PixmapLoader() = default;

	auto pixmap(int width = -1, int height = -1) const -> QPixmap;

	auto pixmapName() const -> const std::string& { return m_name; }

private:
	std::string m_name;
	const char* const* m_xpm = nullptr;
};

} // namespace lmms

#ifdef PLUGIN_NAME

class PluginPixmapLoader : public lmms::PixmapLoader
{
public:
	PluginPixmapLoader() = default;

	explicit PluginPixmapLoader(std::string name, const char* const* xpm = nullptr) :
		lmms::PixmapLoader{LMMS_STRINGIFY(PLUGIN_NAME) "/" + name, xpm}
	{ }
};

namespace PLUGIN_NAME {

inline auto getIconPixmap(std::string_view name,
	int width = -1, int height = -1, const char* const* xpm = nullptr) -> QPixmap
{
	return PluginPixmapLoader{std::string{name}, xpm}.pixmap(width, height);
}

} // namespace PLUGIN_NAME

#endif // PLUGIN_NAME

#endif // LMMS_PIXMAP_LOADER_H
