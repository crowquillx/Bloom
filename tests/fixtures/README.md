# Visual Regression Test Fixtures

This directory contains deterministic test data for visual regression testing of the Bloom HTPC application.

## Files

### `test_library.json`

A comprehensive JSON fixture containing test data for:

- **Libraries**: 2 libraries (Movies, TV Shows)
- **Movies**: 3 movies with varied states:
  - `movie-001`: In-progress (45%), favorite, with multiple audio/subtitle tracks
  - `movie-002`: Fully watched, not favorite
  - `movie-003`: Unwatched, fresh
- **Series**: 2 series:
  - `series-001`: Continuing, with unwatched episodes
  - `series-002`: Ended, fully watched, favorite
- **Seasons**: 3 seasons across the series
- **Episodes**: 5 episodes with varied states:
  - Watched, unwatched, and partially watched
- **Next Up**: 1 episode for the "Next Up" section
- **Resume Items**: 2 items for "Continue Watching"
- **Playback Info**: Sample media source with video/audio/subtitle streams
- **MDBList Ratings**: Sample rating data from multiple sources

### `test_images/placeholder.svg`

A deterministic SVG placeholder image (300x450, 2:3 aspect ratio) used for all image references in the test data. Features:

- Film strip decorations on left and right edges
- Play button icon in center
- "TEST IMAGE" label
- No external dependencies or random elements

## Usage

### In C++ Tests

```cpp
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

// Get fixtures path from CMake variable
QString fixturesPath = TEST_FIXTURES_PATH;

// Load JSON fixture
QFile file(fixturesPath + "/test_library.json");
if (file.open(QIODevice::ReadOnly)) {
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject fixtures = doc.object();
    
    // Access test data
    QJsonArray movies = fixtures["movies"].toObject()["Items"].toArray();
    // ...
}
```

### In QML Tests

```qml
// Load fixture data via Qt.resolvedUrl()
var fixturesPath = Qt.resolvedUrl("../fixtures")
var json = JSON.parse(FileHelper.read(fixturesPath + "/test_library.json"))
```

## Data Characteristics

All data is designed to be **deterministic**:

- **No random values**: All IDs, names, and values are fixed
- **No timestamps**: Dates are fixed strings (e.g., "2024-06-15T00:00:00.000Z")
- **No external dependencies**: All image references point to the placeholder SVG
- **Edge case coverage**: Includes favorites, in-progress, watched, and unwatched states

## Jellyfin API Compatibility

The JSON structure follows Jellyfin API response format:

- `Items` arrays with `TotalRecordCount`
- `UserData` objects with `Played`, `PlaybackPositionTicks`, `IsFavorite`
- `ImageTags` for poster/backdrop/logo references
- `MediaSources` with `MediaStreams` for playback info
- `RunTimeTicks` for duration (1 tick = 100ns)

## Adding New Fixtures

When adding new test data:

1. Use deterministic IDs (e.g., `movie-004`, `series-003`)
2. Include all required fields for the UI component being tested
3. Reference the placeholder SVG for all image tags
4. Update this README with the new data description
5. Ensure no timestamps or random values are introduced
