from tests.conftest import API_ROOT
from wpsync import site_settings as settings_mod
from wpsync.config import Config
from wpsync.wp_client import WPClient


def test_pull_writes_manifest_and_state(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Gone to Look for America", "description": "A ride"})
    settings = settings_mod.pull(project, client)
    assert settings.values["title"] == "Gone to Look for America"

    reloaded = settings_mod.load(project)
    assert reloaded.values["description"] == "A ride"


def test_status_new_then_unchanged_after_pull(requests_mock, project: Config, client: WPClient):
    assert settings_mod.status(project) == "unsynced"
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Title"})
    settings_mod.pull(project, client)
    assert settings_mod.status(project) == "unchanged"


def test_status_modified_after_local_edit(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Title"})
    settings_mod.pull(project, client)

    manifest_path = settings_mod.manifest_path(project)
    manifest_path.write_text("title: Changed Title\n", encoding="utf-8")
    assert settings_mod.status(project) == "modified"


def test_plan_push_unchanged_when_nothing_edited(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Title"})
    settings_mod.pull(project, client)
    plan = settings_mod.plan_push(project)
    assert plan.action == "unchanged"


def test_plan_push_reports_changed_fields(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Title", "description": "Old"})
    settings_mod.pull(project, client)

    settings_mod.manifest_path(project).write_text("title: Title\ndescription: New\n", encoding="utf-8")
    plan = settings_mod.plan_push(project)
    assert plan.action == "would-update"
    assert plan.changed_fields == ["description"]


def test_apply_push_updates_remote_and_state(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Title"})
    settings_mod.pull(project, client)

    settings_mod.manifest_path(project).write_text("title: New Title\n", encoding="utf-8")
    requests_mock.post(f"{API_ROOT}/settings", json={"title": "New Title"})

    outcome = settings_mod.apply_push(project, client)
    assert outcome.action == "update"
    assert settings_mod.status(project) == "unchanged"


def test_apply_push_error_leaves_state_untouched(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Title"})
    settings_mod.pull(project, client)

    settings_mod.manifest_path(project).write_text("title: New Title\n", encoding="utf-8")
    requests_mock.post(f"{API_ROOT}/settings", status_code=500, text="boom")

    outcome = settings_mod.apply_push(project, client)
    assert outcome.action == "error"
    assert settings_mod.status(project) == "modified"


def test_disallowed_fields_are_ignored_on_load(project: Config):
    settings_mod.manifest_path(project).write_text(
        "title: OK\ndefault_role: administrator\n", encoding="utf-8"
    )
    settings = settings_mod.load(project)
    assert "default_role" not in settings.values
    assert settings.values["title"] == "OK"
