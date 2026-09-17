package kewl.plugins.objecthighlighter;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import kewl.api.TileObject;
import kewl.api.TileObjectCategory;

public class ObjectFilterTest
{
    @Test
    public void parsesIdsAndNames()
    {
        ObjectFilter filter = ObjectFilter.parse("Tree, 1276, Oak tree");
        assertTrue(filter.matches(new TileObject(1276, 1, 1, 0, TileObjectCategory.GAME_OBJECT)));
        assertFalse(filter.matches(new TileObject(1277, 1, 1, 0, TileObjectCategory.GAME_OBJECT)));
        assertTrue(filter.isEmpty() == false);
        assertTrue(filter.idCount() == 1);
        assertTrue(filter.nameCount() == 2);
    }
}
