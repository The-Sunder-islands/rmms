#include "PixmapLoader.h"

#include "embed.h"


namespace lmms
{

QPixmap PixmapLoader::pixmap(int width, int height) const
{
	return embed::getIconPixmap(m_name, width, height, m_xpm);
}

} // namespace lmms
