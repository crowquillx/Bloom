## What's Changed in v0.5.1

### New Features

- **ci**: streamline dev release process and update artifact handling
- **movie-details**: align movie details with series layout
- **series-details**: polish layout and restore return focus
- **series**: rework details layout and shelves
- **series**: add recommendation and favorite detail state

### Bug Fixes

- **home**: Set focus property to false for improved interaction in SeriesDetailsView
- **movie-details**: close menu on back input
- **movie-details**: limit similar item retries
- **movie-details**: restore fallback focus paths
- **series-details**: tighten restore guards and response validation
- **series-details**: restore external rating chips
- **series-details**: harden similar item caching and restore guards
- **ui**: harden seerr singleton availability check
- **seerr**: avoid duplicate similar failure signals
- **series**: align similar-items guard on cached loads
- **series**: allow similar-items retry after failure
- **network**: emit auth failures without custom handlers
- **network**: suppress duplicate terminal failure signals
- **series**: avoid duplicate similar-items reload fetches
- **library-service**: keep auth preflight failures scoped
- **series-details**: preserve season zero in next-up label
- **seerr**: emit configured changes only on state flips
- **library-service**: validate similar item ids
- **series-details**: avoid duplicate similar requests in flight
- **series-details**: avoid repeated similar retries after 304
- **library-service**: include 401 error detail in logs
- **series-details**: add mouse wheel shelf scrolling
- **seerr**: react to runtime config changes
- **series-details**: avoid early loaded signal from cache
- **library-service**: keep scoped parse failures private
- **series-details**: emit loaded after seasons complete
- **series-details**: skip duplicate similar fetches on reload
- **library-service**: preserve dedicated 401 expiry flow
- **seerr**: capture similar failure context by value
- **library-service**: emit generic 401 error notifications
- **series-details**: log seerr similar request errors
- **seerr**: stop emitting success after similar failures
- **library**: avoid duplicate series detail requests
- **series-details**: avoid duplicate seerr fetches on cached loads
- **series-details**: correlate seerr failures to tmdb requests
- **series-details**: reload similar items after empty 304 state
- **library-service**: notify scoped failures on 401 responses
- **series-details**: persist favorite toggles to cache
- **series-details**: scope request failures by item id
- **series-details**: avoid similar fetches after 304 hits
- **series-details**: clarify seerr recommendations copy
- **series-details**: skip empty recommendation sections during focus
- **series-details**: include creators in key creatives

### Refactoring

- **Main.qml**: clean up property formatting and improve focus handling logic
- **series-details**: centralize seerr media type
- **seerr**: centralize similar failure signaling

### Documentation

- **viewmodels**: refresh movie details model docs

### Chores

- **ui**: clarify library backdrop refresh comment

**Full changelog:** [v0.5.0...v0.5.1](https://github.com/crowquillx/Bloom/compare/v0.5.0...v0.5.1)
