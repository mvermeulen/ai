from pathlib import Path

from wpsync import insights
from wpsync.config import Config
from wpsync.content import ContentItem


def write_post(project: Config, slug: str, **kwargs) -> ContentItem:
    defaults = dict(
        kind="post",
        path=project.content_dir / "posts" / f"{slug}.md",
        title=slug.title(),
        slug=slug,
        status="publish",
        date="2026-06-01T09:00:00",
        excerpt="A short teaser.",
        featured_image="media/cover.jpg",
        body="Some words go here for the body text of this post about cycling.",
    )
    defaults.update(kwargs)
    item = ContentItem(**defaults)
    item.path.parent.mkdir(parents=True, exist_ok=True)
    item.path.write_text(item.dump(), encoding="utf-8")
    return item


def test_word_count_and_reading_minutes():
    assert insights.word_count("one two three") == 3
    assert insights.reading_minutes(200) == 1
    assert insights.reading_minutes(450) == 2


def test_build_report_basic_counts(project: Config):
    write_post(project, "post-a", categories=["texas"], tags=["cycling"])
    write_post(project, "post-b", kind="page", categories=[], tags=[])

    report = insights.build_report(project, [
        ContentItem.load(project.content_dir / "posts" / "post-a.md"),
        ContentItem.load(project.content_dir / "posts" / "post-b.md"),
    ])
    assert report.total_posts == 1
    assert report.total_pages == 1
    assert report.total_words > 0
    assert report.category_counts == {"texas": 1}


def test_build_report_flags_missing_featured_media(project: Config):
    write_post(project, "post-a", featured_image="media/does-not-exist.jpg")
    item = ContentItem.load(project.content_dir / "posts" / "post-a.md")
    report = insights.build_report(project, [item])
    assert report.items[0].missing_media == ["media/does-not-exist.jpg"]
    warnings = dict(report.accessibility_warnings)
    assert "missing media file" in warnings.get("post-a", "")


def test_build_report_no_warning_when_media_exists(project: Config):
    (project.content_dir / "media" / "cover.jpg").write_bytes(b"fake")
    write_post(project, "post-a")
    item = ContentItem.load(project.content_dir / "posts" / "post-a.md")
    report = insights.build_report(project, [item])
    assert report.items[0].missing_media == []


def test_build_report_detects_images_missing_alt_text(project: Config):
    write_post(project, "post-a", body="![](media/photo.jpg)\n\n![A nice sunset](media/sunset.jpg)")
    item = ContentItem.load(project.content_dir / "posts" / "post-a.md")
    report = insights.build_report(project, [item])
    assert report.items[0].images_missing_alt == 1


def test_build_report_detects_orphaned_media(project: Config):
    (project.content_dir / "media" / "used.jpg").write_bytes(b"fake")
    (project.content_dir / "media" / "orphan.jpg").write_bytes(b"fake")
    write_post(project, "post-a", featured_image="media/used.jpg", body="text only")
    item = ContentItem.load(project.content_dir / "posts" / "post-a.md")
    report = insights.build_report(project, [item])
    assert report.orphaned_media == ["media/orphan.jpg"]


def test_build_report_posts_per_month(project: Config):
    write_post(project, "post-a", date="2026-06-01T09:00:00")
    write_post(project, "post-b", date="2026-06-15T09:00:00")
    write_post(project, "post-c", date="2026-07-01T09:00:00")
    items = [
        ContentItem.load(project.content_dir / "posts" / f"{s}.md") for s in ("post-a", "post-b", "post-c")
    ]
    report = insights.build_report(project, items)
    assert report.posts_per_month == {"2026-06": 2, "2026-07": 1}


def test_build_report_empty_project(project: Config):
    report = insights.build_report(project, [])
    assert report.total_posts == 0
    assert report.total_words == 0
    assert report.average_words == 0.0


def test_format_report_does_not_crash_on_empty_report(project: Config):
    report = insights.build_report(project, [])
    text = insights.format_report(report)
    assert "Posts: 0" in text


def test_format_report_includes_key_sections(project: Config):
    (project.content_dir / "media" / "orphan.jpg").write_bytes(b"fake")
    write_post(project, "post-a", categories=["texas"], tags=["cycling"], excerpt="")
    item = ContentItem.load(project.content_dir / "posts" / "post-a.md")
    report = insights.build_report(project, [item])
    text = insights.format_report(report)
    assert "Posts per month" in text
    assert "Categories" in text
    assert "Orphaned media" in text
    assert "Warnings" in text
