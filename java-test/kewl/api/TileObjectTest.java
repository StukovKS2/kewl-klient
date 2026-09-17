package kewl.api;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.awt.Polygon;

import org.junit.Test;

public class TileObjectTest
{
    @Test
    public void parsesValidFlattenedHull()
    {
        Polygon hull = NativeObjects.parseHull(new int[]{10, 20, 30, 40, 50, 60});
        assertEquals(3, hull.npoints);
        assertEquals(10, hull.xpoints[0]);
        assertEquals(20, hull.ypoints[0]);
        assertEquals(50, hull.xpoints[2]);
        assertEquals(60, hull.ypoints[2]);
    }

    @Test
    public void rejectsInvalidFlattenedHulls()
    {
        assertNull(NativeObjects.parseHull(null));
        assertNull(NativeObjects.parseHull(new int[0]));
        assertNull(NativeObjects.parseHull(new int[]{1, 2}));
        assertNull(NativeObjects.parseHull(new int[]{1, 2, 3}));
        assertNull(NativeObjects.parseHull(new int[]{1, 2, 3, 4, 5}));
    }

    @Test
    public void identityIncludesLocationAndPlane()
    {
        TileObject first = new TileObject(1276, 42, 55, 0, TileObjectCategory.GAME_OBJECT);
        TileObject second = new TileObject(1276, 43, 55, 0, TileObjectCategory.GAME_OBJECT);
        TileObject same = new TileObject(1276, 42, 55, 0, TileObjectCategory.GAME_OBJECT);
        assertEquals(first, same);
        assertEquals(first.getKey(), same.getKey());
        assertEquals(false, first.equals(second));
    }
}
