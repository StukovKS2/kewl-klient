package kewl.api;

/** The kind of scene record that owns a tile object. */
public enum TileObjectCategory
{
    GAME_OBJECT(0x102),
    BOUNDARY_OBJECT(0x100),
    WALL_DECORATION(0x101),
    FLOOR_DECORATION(0x103),
    UNKNOWN(-1);

    private final int nativeValue;

    TileObjectCategory(int nativeValue)
    {
        this.nativeValue = nativeValue;
    }

    static TileObjectCategory fromNative(int value)
    {
        for (TileObjectCategory category : values())
        {
            if (category.nativeValue == value)
            {
                return category;
            }
        }
        return UNKNOWN;
    }
}
