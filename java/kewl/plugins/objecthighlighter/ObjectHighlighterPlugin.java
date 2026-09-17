package kewl.plugins.objecthighlighter;

import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.Graphics2D;
import java.awt.Polygon;
import java.awt.Stroke;

import kewl.Plugin;
import kewl.api.Game;
import kewl.api.TileObject;
import net.runelite.client.ui.overlay.OverlayUtil;

/** Highlights loaded game objects by ID or exact case-insensitive name. */
public final class ObjectHighlighterPlugin extends Plugin
{
    private ObjectFilter filter = ObjectFilter.parse("");
    private String filterText = "";

    public ObjectHighlighterPlugin()
    {
        config.text("objects", "Objects", "Names or IDs separated by commas. Names are exact and case-insensitive.", "");
        config.enumeration("style", "Highlight style", "MODEL uses the current rendered object outline when available.", ObjectHighlightStyle.MODEL_OUTLINE_AND_FILL);
        config.colour("outline", "Outline color", "Object outline color.", Color.CYAN);
        config.colour("fill", "Fill color", "Object model fill color and opacity.", new Color(0, 255, 255, 32));
        config.number("outlineWidth", "Outline width", "Model outline width in pixels.", 2, 1, 8);
        config.bool("fallbackToTile", "Fallback to tile", "Use the object's tile footprint when model geometry is unavailable.", true);
        config.number("maxDistance", "Max distance", "Maximum distance in tiles; 0 means unlimited.", 32, 0, 104);
    }

    @Override public String name() { return "Object Highlighter"; }
    @Override public String description() { return "Highlights trees, rocks, doors, and scenery using their model outline."; }
    @Override public String[] tags() { return new String[]{"objects", "highlight", "loc", "scenery", "overlay"}; }

    @Override
    public void tick()
    {
        String current = config.text("objects");
        if (current.equals(filterText)) return;
        filterText = current;
        filter = ObjectFilter.parse(current);
    }

    @Override
    public String status()
    {
        int objects = Game.tileObjects().size();
        return filter.idCount() + " id / " + filter.nameCount() + " name filters · " + objects
                + " loaded objects" + (objects == 0 ? " (native objects unavailable or scene empty)" : "");
    }

    @Override
    public void render(Graphics2D graphics)
    {
        if (!Game.ready() || filter.isEmpty()) return;

        ObjectHighlightStyle style = style();
        Color outline = config.colour("outline");
        Color fill = config.colour("fill");
        int width = config.number("outlineWidth");
        int maxDistance = config.number("maxDistance");
        boolean fallback = config.bool("fallbackToTile");
        Stroke stroke = new BasicStroke(width, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND);

        for (TileObject object : Game.tileObjects())
        {
            if (!filter.matches(object)) continue;
            if (maxDistance > 0 && object.distance() > maxDistance) continue;

            if (style != ObjectHighlightStyle.TILE)
            {
                Polygon hull = object.getHull();
                if (hull != null && hull.npoints >= 3)
                {
                    drawModel(graphics, hull, style, outline, fill, stroke);
                    continue;
                }
            }

            if (fallback || style == ObjectHighlightStyle.TILE)
            {
                Polygon tile = object.getTilePolygon();
                if (tile != null) OverlayUtil.renderPolygon(graphics, tile, outline, fill, stroke);
            }
        }
    }

    private ObjectHighlightStyle style()
    {
        Object value = config.get("style") == null ? null : config.get("style").value();
        return value instanceof ObjectHighlightStyle s ? s : ObjectHighlightStyle.MODEL_OUTLINE_AND_FILL;
    }

    private static void drawModel(Graphics2D graphics, Polygon hull, ObjectHighlightStyle style,
                                  Color outline, Color fill, Stroke stroke)
    {
        if (style == ObjectHighlightStyle.MODEL_OUTLINE)
        {
            OverlayUtil.renderOutlinedPolygon(graphics, hull, outline, null, stroke);
        }
        else if (style == ObjectHighlightStyle.MODEL_FILL)
        {
            OverlayUtil.renderPolygon(graphics, hull, outline, fill, stroke);
        }
        else
        {
            OverlayUtil.renderOutlinedPolygon(graphics, hull, outline, fill, stroke);
        }
    }
}
