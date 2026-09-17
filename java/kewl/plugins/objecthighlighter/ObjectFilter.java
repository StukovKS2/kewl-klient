package kewl.plugins.objecthighlighter;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

import kewl.api.TileObject;

/** Immutable, allocation-free-per-object filter used by Object Highlighter. */
public final class ObjectFilter
{
    private final Set<Integer> ids;
    private final Set<String> names;

    private ObjectFilter(Set<Integer> ids, Set<String> names)
    {
        this.ids = Set.copyOf(ids);
        this.names = Set.copyOf(names);
    }

    public static ObjectFilter parse(String text)
    {
        Set<Integer> ids = new HashSet<>();
        Set<String> names = new HashSet<>();
        if (text != null)
        {
            for (String token : text.split(","))
            {
                String value = token.trim();
                if (value.isEmpty()) continue;
                try
                {
                    ids.add(Integer.parseInt(value));
                }
                catch (NumberFormatException ignored)
                {
                    names.add(value.toLowerCase(Locale.ROOT));
                }
            }
        }
        return new ObjectFilter(ids, names);
    }

    public boolean matches(TileObject object)
    {
        if (object == null) return false;
        if (ids.contains(object.getId())) return true;
        String name = object.getName();
        return name != null && names.contains(name.toLowerCase(Locale.ROOT));
    }

    public int idCount() { return ids.size(); }
    public int nameCount() { return names.size(); }
    public boolean isEmpty() { return ids.isEmpty() && names.isEmpty(); }
}
