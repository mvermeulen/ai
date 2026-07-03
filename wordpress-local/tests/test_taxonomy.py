import pytest

from tests.conftest import API_ROOT
from wpsync import taxonomy as tax
from wpsync.config import Config
from wpsync.wp_client import WPClient


def test_pull_populates_categories_and_tags(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/categories", json=[{"id": 3, "name": "Texas", "slug": "texas", "parent": 0}])
    requests_mock.get(f"{API_ROOT}/tags", json=[{"id": 8, "name": "trains", "slug": "trains"}])

    manifest = tax.pull(project, client)
    assert manifest.categories[0].name == "Texas"
    assert manifest.tags[0].name == "trains"

    reloaded = tax.load_manifest(project)
    assert reloaded.categories[0].wp_id == 3


def test_pull_resolves_parent_category_name(requests_mock, project: Config, client: WPClient):
    requests_mock.get(
        f"{API_ROOT}/categories",
        json=[
            {"id": 1, "name": "USA", "slug": "usa", "parent": 0},
            {"id": 2, "name": "Texas", "slug": "texas", "parent": 1},
        ],
    )
    requests_mock.get(f"{API_ROOT}/tags", json=[])
    manifest = tax.pull(project, client)
    texas = next(c for c in manifest.categories if c.name == "Texas")
    assert texas.parent == "USA"


def test_pull_preserves_local_drafts(requests_mock, project: Config, client: WPClient):
    tax.add_term(project, "category", "New Mexico", description="Draft category")

    requests_mock.get(f"{API_ROOT}/categories", json=[{"id": 3, "name": "Texas", "slug": "texas", "parent": 0}])
    requests_mock.get(f"{API_ROOT}/tags", json=[])
    manifest = tax.pull(project, client)

    names = sorted(c.name for c in manifest.categories)
    assert names == ["New Mexico", "Texas"]
    draft = next(c for c in manifest.categories if c.name == "New Mexico")
    assert draft.wp_id is None


def test_add_term_rejects_duplicate_name(project: Config):
    tax.add_term(project, "tag", "cycling")
    with pytest.raises(ValueError):
        tax.add_term(project, "tag", "Cycling")


def test_set_description_requires_existing_term(project: Config):
    with pytest.raises(ValueError):
        tax.set_description(project, "tag", "nope", "desc")


def test_plan_push_lists_only_draft_terms(project: Config):
    tax.add_term(project, "category", "New Mexico")
    outcomes = tax.plan_push(project)
    assert [o.name for o in outcomes] == ["New Mexico"]


def test_apply_push_creates_draft_terms_and_assigns_wp_id(requests_mock, project: Config, client: WPClient):
    tax.add_term(project, "category", "New Mexico", description="Land of Enchantment")
    requests_mock.post(f"{API_ROOT}/categories", json={"id": 99, "name": "New Mexico", "slug": "new-mexico"})

    outcomes = tax.apply_push(project, client)
    assert outcomes[0].action == "create"

    manifest = tax.load_manifest(project)
    assert manifest.categories[0].wp_id == 99
    assert manifest.categories[0].slug == "new-mexico"


def test_apply_push_records_error_without_raising(requests_mock, project: Config, client: WPClient):
    tax.add_term(project, "tag", "cycling")
    requests_mock.post(f"{API_ROOT}/tags", status_code=500, text="boom")

    outcomes = tax.apply_push(project, client)
    assert outcomes[0].action == "error"
    manifest = tax.load_manifest(project)
    assert manifest.tags[0].wp_id is None
