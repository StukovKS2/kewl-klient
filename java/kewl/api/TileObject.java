package kewl.api;

import java.awt.Polygon;

/**
 * One currently loaded scene object. This is a frame snapshot; its model hull is deliberately read
 * through the native adapter each time because dynamic objects can animate between frames.
 */
public final class TileObject
{
    private final int id;
    private final int sceneX;
    private final int sceneY;
    private final int plane;
    private final TileObjectCategory category;
    private final TileObjectKey key;

    public TileObject(int id, int sceneX, int sceneY, int plane, TileObjectCategory category)
    {
        this.id = id;
        this.sceneX = sceneX;
        this.sceneY = sceneY;
        this.plane = plane;
        this.category = category == null ? TileObjectCategory.UNKNOWN : category;
        this.key = new TileObjectKey(id, sceneX, sceneY, plane);
    }

    public int getId() { return id; }
    public int getSceneX() { return sceneX; }
    public int getSceneY() { return sceneY; }
    public int getPlane() { return plane; }
    public TileObjectCategory getCategory() { return category; }
    public TileObjectKey getKey() { return key; }

    /** Absolute location corresponding to the object's scene tile. */
    public WorldPoint getWorldLocation()
    {
        return new WorldPoint(Game.sceneBaseX() + sceneX, Game.sceneBaseY() + sceneY);
    }

    /** Repository-style short alias for {@link #getWorldLocation()}. */
    public WorldPoint location() { return getWorldLocation(); }

    /** Immutable LocType display name, or null when this native build cannot resolve definitions. */
    public String getName() { return NativeObjects.name(id); }

    /** Object actions are not exposed by this build yet. */
    public java.util.List<String> getActions() { return java.util.Collections.emptyList(); }

    /** Current projected model outline, or null when native geometry is unavailable. */
    public Polygon getHull()
    {
        return NativeObjects.hull(id, sceneX, sceneY, plane);
    }

    /** Tile/footprint fallback. Size is not currently exposed, so this is the object's origin tile. */
    public Polygon getTilePolygon()
    {
        return Game.tileOutline(sceneX, sceneY);
    }

    /** Chebyshev distance from the local player, or MAX_VALUE when no player exists. */
    public int distance() { return Game.distanceTo(sceneX, sceneY); }

    @Override
    public boolean equals(Object other)
    {
        return other instanceof TileObject object && key.equals(object.key);
    }

    @Override
    public int hashCode() { return key.hashCode(); }

    @Override
    public String toString()
    {
        return "object#" + id + " @" + sceneX + "," + sceneY + "," + plane + " (" + category + ")";
    }
}
