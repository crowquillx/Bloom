import re
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

PROVIDER_WIRE_FIELDS = {
    "Id",
    "Name",
    "Type",
    "Overview",
    "UserData",
    "RunTimeTicks",
    "MediaSources",
    "MediaStreams",
    "ImageTags",
    "BackdropImageTags",
    "ParentBackdropImageTags",
    "ProviderIds",
    "PrimaryImageTag",
    "SeriesPrimaryImageTag",
    "ParentPrimaryImageTag",
    "ProductionYear",
    "SeriesId",
    "SeasonId",
    "ParentId",
    "IndexNumber",
    "ParentIndexNumber",
}

REMOVED_RAW_SIGNAL_NAMES = {
    "viewsLoaded",
    "canonicalViewsLoaded",
    "itemsLoaded",
    "itemsLoadedWithTotal",
    "itemsLoadedWithTotalForQuery",
    "canonicalItemsLoadedWithTotalForQuery",
    "itemsNotModified",
    "itemsNotModifiedForQuery",
    "itemLoaded",
    "itemUserDataChanged",
    "nextUpLoaded",
    "latestMediaLoaded",
    "homeBackdropItemsLoaded",
    "seriesDetailsLoaded",
    "seriesDetailsNotModified",
    "similarItemsLoaded",
    "canonicalSimilarItemsLoaded",
    "similarItemsFailed",
    "heroLibraryItemsLoaded",
    "heroSeriesOverviewsLoaded",
}


def migrated_sources():
    for directory, suffixes in (
        (REPOSITORY_ROOT / "src" / "ui", {".qml", ".cpp", ".h"}),
        (REPOSITORY_ROOT / "src" / "player", {".cpp", ".h"}),
    ):
        for path in directory.rglob("*"):
            if path.suffix in suffixes:
                yield path


def without_comments(text):
    result = []
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if state == "line-comment":
            if current == "\n":
                state = "code"
                result.append(current)
            else:
                result.append(" ")
        elif state == "block-comment":
            if current == "*" and following == "/":
                result.extend((" ", " "))
                index += 1
                state = "code"
            else:
                result.append(current if current == "\n" else " ")
        elif state == "string":
            result.append(current)
            if current == "\\" and following:
                result.append(following)
                index += 1
            elif current == quote:
                state = "code"
        elif current == "/" and following == "/":
            result.extend((" ", " "))
            index += 1
            state = "line-comment"
        elif current == "/" and following == "*":
            result.extend((" ", " "))
            index += 1
            state = "block-comment"
        else:
            result.append(current)
            if current in {"'", '"'}:
                state = "string"
                quote = current

        index += 1
    return "".join(result)


class CanonicalBoundaryContractTest(unittest.TestCase):
    def test_migrated_ui_and_player_do_not_read_provider_wire_fields(self):
        qml_access = re.compile(
            r"(?:\.\s*|\[\s*['\"])("
            + "|".join(sorted(PROVIDER_WIRE_FIELDS))
            + r")(?:\b|['\"])"
        )
        cpp_literal = re.compile(
            r"['\"](" + "|".join(sorted(PROVIDER_WIRE_FIELDS)) + r")[\"']"
        )

        violations = []
        for path in migrated_sources():
            text = without_comments(path.read_text(encoding="utf-8"))
            pattern = qml_access if path.suffix == ".qml" else cpp_literal
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                violations.append(
                    f"{path.relative_to(REPOSITORY_ROOT)}:{line}: {match.group(1)}"
                )

        self.assertEqual(
            violations,
            [],
            "provider wire fields leaked into migrated UI/player code:\n"
            + "\n".join(violations),
        )

    def test_migrated_ui_and_player_do_not_include_provider_adapters(self):
        include = re.compile(r"^\s*#\s*include\s*[<\"][^>\"]*providers/(?:jellyfin|silo)/", re.MULTILINE)
        violations = []
        for path in migrated_sources():
            if path.suffix == ".qml":
                continue
            if include.search(path.read_text(encoding="utf-8")):
                violations.append(str(path.relative_to(REPOSITORY_ROOT)))

        self.assertEqual(
            violations,
            [],
            "provider adapters must stay outside migrated UI/player code",
        )

    def test_library_service_does_not_republish_removed_raw_dto_signals(self):
        header = (REPOSITORY_ROOT / "src/network/LibraryService.h").read_text(
            encoding="utf-8"
        )
        implementation = (
            REPOSITORY_ROOT / "src/network/LibraryService.cpp"
        ).read_text(encoding="utf-8")

        violations = []
        for name in sorted(REMOVED_RAW_SIGNAL_NAMES):
            if re.search(rf"\bvoid\s+{re.escape(name)}\s*\(", header):
                violations.append(f"declaration: {name}")
            if re.search(rf"\bemit\s+{re.escape(name)}\s*\(", implementation):
                violations.append(f"emission: {name}")

        self.assertEqual(
            violations,
            [],
            "obsolete provider-shaped compatibility signals returned:\n"
            + "\n".join(violations),
        )

    def test_library_ui_and_view_model_use_canonical_sort_keys(self):
        sources = (
            REPOSITORY_ROOT / "src" / "ui" / "LibraryScreen.qml",
            REPOSITORY_ROOT / "src" / "viewmodels" / "LibraryViewModel.cpp",
        )
        provider_sort_tokens = {
            "SortName",
            "PremiereDate",
            "DateCreated",
            "CommunityRating",
            "ProductionYear",
            "Random",
            "Ascending",
            "Descending",
        }
        violations = []
        for path in sources:
            text = without_comments(path.read_text(encoding="utf-8"))
            if path.suffix == ".qml":
                text = "\n".join(
                    re.findall(r"\bvalues\s*:\s*\[[^\]]*\]", text)
                )
            for token in sorted(provider_sort_tokens):
                if re.search(rf"['\"]{token}['\"]", text):
                    violations.append(
                        f"{path.relative_to(REPOSITORY_ROOT)}: {token}"
                    )

        self.assertEqual(
            violations,
            [],
            "provider-native sort tokens leaked past the adapter boundary:\n"
            + "\n".join(violations),
        )


if __name__ == "__main__":
    unittest.main()
