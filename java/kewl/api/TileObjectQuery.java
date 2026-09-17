package kewl.api;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** Query the current loaded scene objects without exposing native storage details. */
public final class TileObjectQuery
{
    private final List<TileObject> source;
    private final Set<Integer> ids = new HashSet<>();
    private final Set<String> names = new HashSet<>();
    private Integer maxDistance;
    private Integer sceneX;
    private Integer sceneY;
    private Integer plane;

    public TileObjectQuery() { this(Game.tileObjects()); }

    public TileObjectQuery(List<TileObject> source)
    {
        this.source = source == null ? List.of() : source;
    }

    public TileObjectQuery ids(int... values)
    {
        if (values != null) for (int value : values) ids.add(value);
        return this;
    }

    public TileObjectQuery names(String... values)
    {
        if (values != null)
        {
            for (String value : values)
            {
                if (value != null && !value.isBlank()) names.add(value.trim().toLowerCase(Locale.ROOT));
            }
        }
        return this;
    }

    public TileObjectQuery within(int tiles) { maxDistance = Math.max(0, tiles); return this; }

    public TileObjectQuery at(int sceneX, int sceneY, int plane)
    {
        this.sceneX = sceneX;
        this.sceneY = sceneY;
        this.plane = plane;
        return this;
    }

    public List<TileObject> result()
    {
        List<TileObject> result = new ArrayList<>();
        for (TileObject object : source)
        {
            if (!ids.isEmpty() && !ids.contains(object.getId()))
            {
                String name = object.getName();
                if (name == null || !names.contains(name.toLowerCase(Locale.ROOT))) continue;
            }
            else if (ids.isEmpty() && !names.isEmpty())
            {
                String name = object.getName();
                if (name == null || !names.contains(name.toLowerCase(Locale.ROOT))) continue;
            }
            if (maxDistance != null && object.distance() > maxDistance) continue;
            if (sceneX != null && (object.getSceneX() != sceneX || object.getSceneY() != sceneY || object.getPlane() != plane)) continue;
            result.add(object);
        }
        return result;
    }

    public TileObject first()
    {
        List<TileObject> result = result();
        return result.isEmpty() ? null : result.get(0);
    }
}
