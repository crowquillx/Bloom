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

    /// The field that tells entries apart: "itemId" (one card per title) for
    /// discovery lists, "requestId" for request lists, where one title can
    /// have several requests.
    property string keyRole: "itemId"

    // Paging, for a list read page after page.

    /// The page to read next.
    property int nextPage: 1
    property bool hasMore: false
    /// The page being read; 0 when none is.
    property int loadingPage: 0
    /// Pages in a row that brought only titles already shown.
    property int repeatedPages: 0
    /// Pages of nothing new to read past before giving up.
    readonly property int maxRepeatedPages: 3

    function entryKey(entry) {
        return String(entry[model.keyRole] || entry.itemId || "")
    }

    /// Appends the entries not already in the model; returns how many were new.
    function appendItems(items) {
        var seen = {}
        for (var i = 0; i < model.count; ++i)
            seen[model.entryKey(model.get(i))] = true
        var added = 0
        for (var j = 0; j < (items || []).length; ++j) {
            var item = items[j]
            if (!item || seen[model.entryKey(item)])
                continue
            seen[model.entryKey(item)] = true
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

    /// Starts reading the next page if there is one and no read is under
    /// way; returns that page, else 0.
    function beginNextPage() {
        if (!model.hasMore || model.loadingPage > 0 || model.nextPage < 1)
            return 0
        model.loadingPage = model.nextPage
        return model.loadingPage
    }

    /// Takes page @p page of the list, a row {items, nextPage, hasMore}, and
    /// returns the page to read at once, else 0. Lists shift between reads,
    /// so a page can repeat titles already shown: the list keeps following
    /// the server's pages past those, maxRepeatedPages at most, and never
    /// reads the same page again. A page other than the one being read is
    /// ignored.
    function acceptPage(page, row) {
        if (page < 1 || page !== model.loadingPage)
            return 0
        model.loadingPage = 0
        var added = model.appendItems(row.items || [])
        var next = row.nextPage || page + 1
        model.repeatedPages = added > 0 ? 0 : model.repeatedPages + 1
        model.nextPage = next
        model.hasMore = !!row.hasMore && next > page && model.repeatedPages < model.maxRepeatedPages
        if (added > 0 || !model.hasMore)
            return 0
        model.loadingPage = next
        return next
    }

    /// Stops paging after page @p page failed; false when it was not the
    /// page being read.
    function failPage(page) {
        if (page < 1 || page !== model.loadingPage)
            return false
        model.loadingPage = 0
        model.hasMore = false
        return true
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
