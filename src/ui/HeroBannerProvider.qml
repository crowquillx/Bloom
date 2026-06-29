import QtQuick

import BloomUI

// Non-visual data provider that builds the hero model from the selected source.
// Reuses data already fetched by HomeScreen (recentlyAddedMap, nextUpModel,
// continueWatchingModel, displayedNextUpModel) for sources 1-3 and mixed.
// Only the "library" source issues a new network request via LibraryService.
QtObject {
    id: provider

    // Populated by HomeScreen bindings (see HomeScreen.qml).
    property var librariesModel: []
    property var recentlyAddedMap: ({})
    property var nextUpModel: []
    property var continueWatchingModel: []
    property var displayedNextUpModel: []

    // The final hero list: array of raw Jellyfin item objects with an added
    // __heroReason string describing why the item is featured.
    property var heroModel: []
    property var seriesOverviewCache: ({})
    property var pendingSeriesOverviewIds: ({})

    // True while the Library source fetch is in flight.
    property bool loading: false

    readonly property bool hasContent: heroModel.length > 0

    // Source badge label for a given item.
    function badgeLabel(reason) {
        switch (reason) {
            case "Recently Added": return qsTr("Recently Added")
            case "Continue Watching": return qsTr("Continue Watching")
            case "Up Next": return qsTr("Up Next")
            case "From Your Library": return qsTr("From Your Library")
            default: return ""
        }
    }

    // Reserved for future subtitle copy; badge alone is shown for now.
    function reasonLine(reason) {
        return ""
    }

    function reset() {
        heroModel = []
        loading = false
        pendingSeriesOverviewIds = ({})
    }

    function rebuild() {
        var source = ConfigManager.heroBannerSource
        var maxItems = ConfigManager.heroBannerMaxItems
        var hiddenTypes = ConfigManager.heroBannerHiddenItemTypes || []

        if (source === "library") {
            // Library source is fetched asynchronously; trigger the request and
            // let onHeroLibraryItemsLoaded assemble the model.
            fetchLibrarySource(maxItems, hiddenTypes)
            return
        }

        var pool = []
        if (source === "recentlyAdded") {
            pool = sampleRecentlyAdded(maxItems, hiddenTypes)
        } else if (source === "continueWatching") {
            pool = tagItems(continueWatchingModel, "Continue Watching", maxItems, hiddenTypes)
        } else if (source === "upNext") {
            pool = tagItems(displayedNextUpModel, "Up Next", maxItems, hiddenTypes)
        } else if (source === "mixed") {
            // Combine Continue Watching + Recently Added (+ Up Next to fill).
            pool = tagItems(continueWatchingModel, "Continue Watching", maxItems, hiddenTypes)
            if (pool.length < maxItems) {
                pool = pool.concat(sampleRecentlyAdded(maxItems - pool.length, hiddenTypes, pool))
            }
            if (pool.length < maxItems) {
                pool = pool.concat(tagItems(displayedNextUpModel, "Up Next", maxItems - pool.length, hiddenTypes, pool))
            }
        }
        heroModel = hydrateSeriesOverviews(pool)
        requestMissingSeriesOverviews(heroModel)
    }

    function sampleRecentlyAdded(maxItems, hiddenTypes, existingPool) {
        var result = []
        var seen = buildSeenSet(existingPool)
        // Iterate libraries in order; round-robin so one large library doesn't dominate.
        var libs = librariesModel
        var perLib = []
        for (var i = 0; i < libs.length; i++) {
            var items = recentlyAddedMap[libs[i].Id] || []
            perLib.push(items)
        }
        var idx = 0
        var anyLeft = true
        while (result.length < maxItems && anyLeft) {
            anyLeft = false
            for (var l = 0; l < perLib.length; l++) {
                if (idx < perLib[l].length) {
                    anyLeft = true
                    var item = perLib[l][idx]
                    if (isEligible(item, hiddenTypes) && !alreadySeen(item, seen)) {
                        var copy = prepareHeroItem(item)
                        copy.__heroReason = "Recently Added"
                        result.push(copy)
                        markSeen(item, seen)
                        if (result.length >= maxItems) break
                    }
                }
            }
            idx++
        }
        return result
    }

    function tagItems(model, reason, maxItems, hiddenTypes, existingPool) {
        var result = []
        var seen = buildSeenSet(existingPool)
        for (var i = 0; i < model.length && result.length < maxItems; i++) {
            var item = model[i]
            if (!isEligible(item, hiddenTypes) || alreadySeen(item, seen)) continue
            var copy = prepareHeroItem(item)
            copy.__heroReason = reason
            result.push(copy)
            markSeen(item, seen)
        }
        return result
    }

    function fetchLibrarySource(maxItems, hiddenTypes) {
        loading = true
        var libraryIds = ConfigManager.heroBannerLibraryIds || []
        var unwatchedOnly = ConfigManager.heroBannerLibraryUnwatchedOnly
        // Request more than we need so hidden-type filtering still fills the cap.
        var requestLimit = Math.min(25, maxItems + 8)
        LibraryService.getHeroLibraryItems(requestLimit, libraryIds, unwatchedOnly)
    }

    function onHeroLibraryItemsLoaded(items) {
        if (!loading) return // stale response
        loading = false
        var maxItems = ConfigManager.heroBannerMaxItems
        var hiddenTypes = ConfigManager.heroBannerHiddenItemTypes || []
        var result = []
        for (var i = 0; i < items.length && result.length < maxItems; i++) {
            var item = items[i]
            if (!isEligible(item, hiddenTypes)) continue
            var copy = prepareHeroItem(item)
            copy.__heroReason = "From Your Library"
            result.push(copy)
        }
        heroModel = hydrateSeriesOverviews(result)
        requestMissingSeriesOverviews(heroModel)
    }

    function isEligible(item, hiddenTypes) {
        if (!item || !item.Id) return false
        var type = item.Type || ""
        for (var i = 0; i < hiddenTypes.length; i++) {
            if (type === hiddenTypes[i]) return false
        }
        // Must have at least one usable image (backdrop, primary, or parent backdrop).
        if (!hasAnyImage(item)) return false
        return true
    }

    function hasAnyImage(item) {
        if (item.BackdropImageTags && item.BackdropImageTags.length > 0) return true
        if (item.ImageTags && (item.ImageTags.Backdrop || item.ImageTags.Primary || item.ImageTags.Logo)) return true
        if (item.ParentBackdropImageTags && item.ParentBackdropImageTags.length > 0) return true
        if (item.SeriesThumbImageTag || item.ParentThumbImageTag) return true
        return false
    }

    function shallowClone(item) {
        var copy = {}
        for (var k in item) {
            if (Object.prototype.hasOwnProperty.call(item, k)) copy[k] = item[k]
        }
        return copy
    }

    function prepareHeroItem(item) {
        var copy = shallowClone(item)
        if (copy.Type === "Episode") {
            var seriesId = copy.SeriesId || ""
            if (seriesId && seriesOverviewCache[seriesId] !== undefined) {
                copy.__seriesOverview = seriesOverviewCache[seriesId] || ""
            }
        }
        return copy
    }

    function hydrateSeriesOverviews(items) {
        var result = []
        for (var i = 0; i < items.length; i++) {
            var copy = shallowClone(items[i])
            if (copy.Type === "Episode") {
                var seriesId = copy.SeriesId || ""
                if (seriesId && seriesOverviewCache[seriesId] !== undefined) {
                    copy.__seriesOverview = seriesOverviewCache[seriesId] || ""
                }
            }
            result.push(copy)
        }
        return result
    }

    function requestMissingSeriesOverviews(items) {
        var ids = []
        var pending = {}
        for (var k in pendingSeriesOverviewIds) {
            if (Object.prototype.hasOwnProperty.call(pendingSeriesOverviewIds, k)) {
                pending[k] = pendingSeriesOverviewIds[k]
            }
        }
        for (var i = 0; i < items.length; i++) {
            var item = items[i]
            if (!item || item.Type !== "Episode") continue
            var seriesId = item.SeriesId || ""
            if (!seriesId) continue
            if (seriesOverviewCache[seriesId] !== undefined || pending[seriesId]) continue
            pending[seriesId] = true
            ids.push(seriesId)
        }
        if (ids.length <= 0) return
        pendingSeriesOverviewIds = pending
        LibraryService.getHeroSeriesOverviews(ids)
    }

    function handleHeroSeriesOverviewsLoaded(overviewsBySeriesId) {
        var cache = {}
        for (var existing in seriesOverviewCache) {
            if (Object.prototype.hasOwnProperty.call(seriesOverviewCache, existing)) {
                cache[existing] = seriesOverviewCache[existing]
            }
        }

        var pending = {}
        for (var p in pendingSeriesOverviewIds) {
            if (Object.prototype.hasOwnProperty.call(pendingSeriesOverviewIds, p)) {
                pending[p] = pendingSeriesOverviewIds[p]
            }
        }

        for (var id in overviewsBySeriesId) {
            if (!Object.prototype.hasOwnProperty.call(overviewsBySeriesId, id)) continue
            cache[id] = overviewsBySeriesId[id] || ""
            delete pending[id]
        }

        seriesOverviewCache = cache
        pendingSeriesOverviewIds = pending
        heroModel = hydrateSeriesOverviews(heroModel)
    }

    function buildSeenSet(existingPool) {
        var seen = {}
        if (existingPool) {
            for (var i = 0; i < existingPool.length; i++) {
                seen[existingPool[i].Id] = true
            }
        }
        return seen
    }

    function alreadySeen(item, seen) {
        return !!seen[item.Id]
    }

    function markSeen(item, seen) {
        seen[item.Id] = true
    }

    // External trigger: HomeScreen calls this when the Library source fetch
    // completes via the LibraryService.heroLibraryItemsLoaded signal.
    function handleHeroLibraryItemsLoaded(items) {
        onHeroLibraryItemsLoaded(items)
    }
}
