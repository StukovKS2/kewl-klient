package kewl.api;

import java.awt.Polygon;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import kewl.Natives;

/** Package-private bridge from JNI serialization to SDK scene objects and Java2D geometry. */
final class NativeObjects
{
    private static final int OBJECT_STRIDE = 5;
    private static final int[] EMPTY = new int[0];
    private static volatile boolean objectsAvailable = true;
    private static volatile boolean hullAvailable = true;
    private static volatile boolean namesAvailable = true;
    private static final Map<Integer, String> NAMES = new HashMap<>();

    private NativeObjects() {}

    static List<TileObject> readObjects()
    {
        if (!objectsAvailable) return Collections.emptyList();

        final int[] raw;
        try
        {
            raw = Natives.objects();
        }
        catch (UnsatisfiedLinkError ex)
        {
            objectsAvailable = false;
            return Collections.emptyList();
        }
        if (raw == null || raw.length < OBJECT_STRIDE) return Collections.emptyList();

        List<TileObject> result = new ArrayList<>(raw.length / OBJECT_STRIDE);
        for (int i = 0; i + OBJECT_STRIDE <= raw.length; i += OBJECT_STRIDE)
        {
            int id = raw[i];
            int sceneX = raw[i + 1];
            int sceneY = raw[i + 2];
            int plane = raw[i + 3];
            if (id < 0 || sceneX < 0 || sceneY < 0 || sceneX >= 104 || sceneY >= 104 || plane < 0 || plane > 3)
            {
                continue;
            }
            result.add(new TileObject(id, sceneX, sceneY, plane, TileObjectCategory.fromNative(raw[i + 4])));
        }
        return Collections.unmodifiableList(result);
    }

    static String name(int id)
    {
        if (id < 0 || !namesAvailable) return null;
        String cached = NAMES.get(id);
        if (cached != null) return cached;
        final String value;
        try
        {
            value = Natives.objectName(id);
        }
        catch (UnsatisfiedLinkError ex)
        {
            namesAvailable = false;
            NAMES.clear();
            return null;
        }
        if (value == null || value.isBlank()) return null;
        if (NAMES.size() > 4096) NAMES.clear();
        NAMES.put(id, value);
        return value;
    }

    static Polygon hull(int id, int sceneX, int sceneY, int plane)
    {
        if (!hullAvailable) return null;

        final int[] raw;
        try
        {
            raw = Natives.objectHull(id, sceneX, sceneY, plane);
        }
        catch (UnsatisfiedLinkError ex)
        {
            hullAvailable = false;
            return null;
        }
        return parseHull(raw);
    }

    /** Converts the optional native flat wire format to a normal Java2D polygon. */
    static Polygon parseHull(int[] raw)
    {
        if (raw == null || raw.length < 6 || (raw.length & 1) != 0) return null;
        Polygon polygon = new Polygon();
        for (int i = 0; i < raw.length; i += 2)
        {
            polygon.addPoint(raw[i], raw[i + 1]);
        }
        return polygon.npoints >= 3 ? polygon : null;
    }
}
