package kewl.api;

import java.util.Objects;

/** Identity of one scene object; ids alone are not unique in a scene. */
public final class TileObjectKey
{
    private final int id;
    private final int sceneX;
    private final int sceneY;
    private final int plane;

    public TileObjectKey(int id, int sceneX, int sceneY, int plane)
    {
        this.id = id;
        this.sceneX = sceneX;
        this.sceneY = sceneY;
        this.plane = plane;
    }

    public int id() { return id; }
    public int sceneX() { return sceneX; }
    public int sceneY() { return sceneY; }
    public int plane() { return plane; }

    @Override
    public boolean equals(Object other)
    {
        if (!(other instanceof TileObjectKey key)) return false;
        return id == key.id && sceneX == key.sceneX && sceneY == key.sceneY && plane == key.plane;
    }

    @Override
    public int hashCode()
    {
        return Objects.hash(id, sceneX, sceneY, plane);
    }

    @Override
    public String toString()
    {
        return id + "@" + sceneX + "," + sceneY + "," + plane;
    }
}
