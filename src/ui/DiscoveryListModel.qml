import QtQuick

/**
 * DiscoveryListModel - a ListModel of request discovery items for shelves
 * and grids that load page after page (docs/seerr.md).
 *
 * Appending keeps the focused card in place, unlike replacing a JS array.
 * Every card gets the same fields, so the roles and their types do not
 * depend on the first title.
 */
ListModel {
    id: model

    /// Appends the titles not already in the model; returns how many were new.
    function appendItems(items) {
        var seen = {}
        for (var i = 0; i < model.count; ++i)
            seen[model.get(i).itemId] = true
        var added = 0
        for (var j = 0; j < (items || []).length; ++j) {
            var item = items[j]
            if (!item || seen[item.itemId])
                continue
            seen[item.itemId] = true
            model.append({
                source: "request",
                requestBackend: item.requestBackend || "",
                itemId: item.itemId || "",
                requestMediaType: item.requestMediaType || "",
                requestTmdbId: Number(item.requestTmdbId || 0),
                name: item.name || "",
                mediaType: item.mediaType || "",
                productionYear: Number(item.productionYear || 0),
                overview: item.overview || "",
                imageUrl: item.imageUrl || "",
                backdropUrl: item.backdropUrl || "",
                requestStatus: item.requestStatus || "",
                requestable: item.requestable !== false,
                libraryItemId: item.libraryItemId || "",
                requestId: String(item.requestId || ""),
                canCancel: !!item.canCancel
            })
            ++added
        }
        return added
    }

    /// Shows a request's new state on the title's cards.
    function setRequestStatus(mediaType, tmdbId, status) {
        for (var i = 0; i < model.count; ++i) {
            var card = model.get(i)
            if (card.requestMediaType === mediaType && Number(card.requestTmdbId) === Number(tmdbId)) {
                model.setProperty(i, "requestStatus", status)
                model.setProperty(i, "requestable", status === "")
                if (status === "") {
                    model.setProperty(i, "requestId", "")
                    model.setProperty(i, "canCancel", false)
                }
            }
        }
    }
}
