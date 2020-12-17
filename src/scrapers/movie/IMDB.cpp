#include "IMDB.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QWidget>

#include "data/Storage.h"
#include "globals/Helper.h"
#include "network/NetworkRequest.h"
#include "scrapers/movie/imdb/ImdbMovieScraper.h"
#include "settings/Settings.h"
#include "ui/main/MainWindow.h"

namespace mediaelch {
namespace scraper {

static int monthNameToInt(const QString& monthName)
{
    if (monthName.contains("January", Qt::CaseInsensitive)) {
        return 1;
    } else if (monthName.contains("February", Qt::CaseInsensitive)) {
        return 2;
    } else if (monthName.contains("March", Qt::CaseInsensitive)) {
        return 3;
    } else if (monthName.contains("April", Qt::CaseInsensitive)) {
        return 4;
    } else if (monthName.contains("May", Qt::CaseInsensitive)) {
        return 5;
    } else if (monthName.contains("June", Qt::CaseInsensitive)) {
        return 6;
    } else if (monthName.contains("July", Qt::CaseInsensitive)) {
        return 7;
    } else if (monthName.contains("August", Qt::CaseInsensitive)) {
        return 8;
    } else if (monthName.contains("September", Qt::CaseInsensitive)) {
        return 9;
    } else if (monthName.contains("October", Qt::CaseInsensitive)) {
        return 10;
    } else if (monthName.contains("November", Qt::CaseInsensitive)) {
        return 11;
    } else if (monthName.contains("December", Qt::CaseInsensitive)) {
        return 12;
    }
    return -1;
}

IMDB::IMDB(QObject* parent) : MovieScraper(parent)
{
    m_meta.identifier = ID;
    m_meta.name = "IMDb";
    m_meta.description = tr("IMDb is the world's most popular and authoritative source for movie, TV "
                            "and celebrity content, designed to help fans explore the world of movies "
                            "and shows and decide what to watch.");
    m_meta.website = "https://www.imdb.com/whats-on-tv/";
    m_meta.termsOfService = "https://www.imdb.com/conditions";
    m_meta.privacyPolicy = "https://www.imdb.com/privacy";
    m_meta.help = "https://help.imdb.com";
    m_meta.supportedDetails = {MovieScraperInfo::Title,
        MovieScraperInfo::Director,
        MovieScraperInfo::Writer,
        MovieScraperInfo::Genres,
        MovieScraperInfo::Tags,
        MovieScraperInfo::Released,
        MovieScraperInfo::Certification,
        MovieScraperInfo::Runtime,
        MovieScraperInfo::Overview,
        MovieScraperInfo::Rating,
        MovieScraperInfo::Tagline,
        MovieScraperInfo::Studios,
        MovieScraperInfo::Countries,
        MovieScraperInfo::Actors,
        MovieScraperInfo::Poster};
    m_meta.supportedLanguages = {"en"};
    m_meta.defaultLocale = "en";
    m_meta.isAdult = false;

    m_settingsWidget = new QWidget(MainWindow::instance());
    m_loadAllTagsWidget = new QCheckBox(tr("Load all tags"), m_settingsWidget);
    auto* layout = new QGridLayout(m_settingsWidget);
    layout->addWidget(m_loadAllTagsWidget, 0, 0);
    layout->setContentsMargins(12, 0, 12, 12);
    m_settingsWidget->setLayout(layout);
}

const MovieScraper::ScraperMeta& IMDB::meta() const
{
    return m_meta;
}

bool IMDB::hasSettings() const
{
    return true;
}

QWidget* IMDB::settingsWidget()
{
    return m_settingsWidget;
}

void IMDB::loadSettings(ScraperSettings& settings)
{
    m_loadAllTags = settings.valueBool("LoadAllTags", false);
    m_loadAllTagsWidget->setChecked(m_loadAllTags);
}

void IMDB::saveSettings(ScraperSettings& settings)
{
    m_loadAllTags = m_loadAllTagsWidget->isChecked();
    settings.setBool("LoadAllTags", m_loadAllTags);
}

QSet<MovieScraperInfo> IMDB::scraperNativelySupports()
{
    return m_meta.supportedDetails;
}

void IMDB::changeLanguage(mediaelch::Locale /*locale*/)
{
    // no-op: Only one language is supported and it is hard-coded.
}

void IMDB::search(QString searchStr)
{
    QString encodedSearch = QUrl::toPercentEncoding(searchStr);

    QRegularExpression rx("^tt\\d+$");
    if (rx.match(searchStr).hasMatch()) {
        QUrl url = QUrl(QStringLiteral("https://www.imdb.com/title/%1/").arg(searchStr).toUtf8());
        QNetworkRequest request(url);
        request.setRawHeader("Accept-Language", "en-US,en;q=0.9"); // todo: add language dropdown in settings
        QNetworkReply* reply = m_network.getWithWatcher(request);
        connect(reply, &QNetworkReply::finished, this, &IMDB::onSearchIdFinished);

    } else {
        QUrl url;
        if (Settings::instance()->showAdultScrapers()) {
            url = QUrl::fromEncoded(QStringLiteral("https://www.imdb.com/search/title/"
                                                   "?adult=include&title_type=feature,documentary,tv_movie,short,video&"
                                                   "view=simple&count=100&title=%1")
                                        .arg(encodedSearch)
                                        .toUtf8());
        } else {
            url = QUrl::fromEncoded(
                QStringLiteral("https://www.imdb.com/find?s=tt&ttype=ft&ref_=fn_ft&q=%1").arg(encodedSearch).toUtf8());
        }

        QNetworkRequest request(url);
        request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
        QNetworkReply* reply = m_network.getWithWatcher(request);
        connect(reply, &QNetworkReply::finished, this, &IMDB::onSearchFinished);
    }
}

void IMDB::onSearchFinished()
{
    auto* reply = dynamic_cast<QNetworkReply*>(QObject::sender());
    if (reply == nullptr) {
        qCritical() << "[IMDb] onSearchFinished: nullptr reply | Please report this issue!";
        emit searchDone(
            {}, {ScraperError::Type::InternalError, tr("Internal Error: Please report!"), "nullptr dereference"});
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[IMDb] Search: Network Error" << reply->errorString();
        emit searchDone({}, mediaelch::replyToScraperError(*reply));
        return;
    }

    QString msg = QString::fromUtf8(reply->readAll());
    auto results = parseSearch(msg);
    emit searchDone(results, {});
}

void IMDB::onSearchIdFinished()
{
    auto* reply = dynamic_cast<QNetworkReply*>(QObject::sender());
    QVector<ScraperSearchResult> results;
    if (reply->error() == QNetworkReply::NoError) {
        QString msg = QString::fromUtf8(reply->readAll());
        ScraperSearchResult result;

        QRegularExpression rx;
        rx.setPatternOptions(
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
        QRegularExpressionMatch match;

        rx.setPattern(R"(<h1 class="header"> <span class="itemprop" itemprop="name">(.*)</span>)");
        match = rx.match(msg);
        if (match.hasMatch()) {
            result.name = match.captured(1);

            rx.setPattern("<h1 class=\"header\"> <span class=\"itemprop\" itemprop=\"name\">.*<span "
                          "class=\"nobr\">\\(<a href=\"[^\"]*\" >([0-9]*)</a>\\)</span>");
            match = rx.match(msg);
            if (match.hasMatch()) {
                result.released = QDate::fromString(match.captured(1), "yyyy");

            } else {
                rx.setPattern("<h1 class=\"header\"> <span class=\"itemprop\" itemprop=\"name\">.*</span>.*<span "
                              "class=\"nobr\">\\(([0-9]*)\\)</span>");
                match = rx.match(msg);
                if (match.hasMatch()) {
                    result.released = QDate::fromString(match.captured(1), "yyyy");
                }
            }
        } else {
            rx.setPattern(R"(<h1 class="">(.*)&nbsp;<span id="titleYear">\(<a href="/year/([0-9]+)/\?ref_=tt_ov_inf")");
            match = rx.match(msg);
            if (match.hasMatch()) {
                result.name = match.captured(1);
                result.released = QDate::fromString(match.captured(2), "yyyy");
            }
        }

        rx.setPattern(R"(<link rel="canonical" href="https://www.imdb.com/title/(.*)/" />)");
        match = rx.match(msg);
        if (match.hasMatch()) {
            result.id = match.captured(1);
        }

        if ((!result.id.isEmpty()) && (!result.name.isEmpty())) {
            results.append(result);
        }
    } else {
        qWarning() << "Network Error" << reply->errorString();
    }

    reply->deleteLater();
    emit searchDone(results, {});
}

QVector<ScraperSearchResult> IMDB::parseSearch(const QString& html)
{
    QVector<ScraperSearchResult> results;
    QRegularExpression rx;

    if (html.contains("Including Adult Titles")) {
        // Search result table from "https://www.imdb.com/search/title/?title=..."
        rx.setPattern(R"(<a href="/title/(tt[\d]+)/[^"]*"\n>([^<]*)</a>\n.*(?: \(I+\) |>)\(([0-9]*).*\))");
    } else {
        // Search result table from "https://www.imdb.com/find?q=..."
        rx.setPattern("<td class=\"result_text\"> <a href=\"/title/([t]*[\\d]+)/[^\"]*\" >([^<]*)</a>(?: \\(I+\\) | "
                      ")\\(([0-9]*)\\) (?:</td>|<br/>)");
    }

    rx.setPatternOptions(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatchIterator matches = rx.globalMatch(html);

    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        ScraperSearchResult result;
        result.name = match.captured(2);
        result.id = match.captured(1);
        result.released = QDate::fromString(match.captured(3), "yyyy");
        results.append(result);
    }
    return results;
}

void IMDB::extractReleased(Movie* movie, const QString& html) const
{
    QRegularExpression rx;
    rx.setPatternOptions(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatch match;

    rx.setPattern("<a href=\"[^\"]*\"(.*)title=\"See all release dates\" >[^<]*<meta itemprop=\"datePublished\" "
                  "content=\"([^\"]*)\" />");
    match = rx.match(html);
    if (match.hasMatch()) {
        movie->setReleased(QDate::fromString(match.captured(2), "yyyy-MM-dd"));

    } else {
        // 2020-12 version
        QString pattern = R"re(href="/title/%1/releaseinfo\?[^"]+">([A-Za-z]+) (\d{1,2}), (\d{4}))re";
        rx.setPattern(pattern.arg(movie->imdbId().toString()));
        match = rx.match(html);
        if (match.hasMatch()) {
            int day = match.captured(2).trimmed().toInt();
            QString monthName = match.captured(1).trimmed();
            int month = monthNameToInt(monthName);
            int year = match.captured(3).trimmed().toInt();

            if (day != 0 && month > -1 && year != 0) {
                movie->setReleased(QDate(year, month, day));
            }

        } else {
            // older version
            rx.setPattern(R"(<h4 class="inline">Release Date:</h4> ([0-9]+) ([A-Za-z]*) ([0-9]{4}))");
            match = rx.match(html);
            if (match.hasMatch()) {
                int day = match.captured(1).trimmed().toInt();
                QString monthName = match.captured(2).trimmed();
                int month = monthNameToInt(monthName);
                int year = match.captured(3).trimmed().toInt();

                if (day != 0 && month > -1 && year != 0) {
                    movie->setReleased(QDate(year, month, day));
                }

            } else {
                rx.setPattern(R"(<title>[^<]+(?:\(| )(\d{4})\) - IMDb</title>)");
                match = rx.match(html);
                if (match.hasMatch()) {
                    const int day = 1;
                    const int month = 1;
                    const int year = match.captured(1).trimmed().toInt();
                    movie->setReleased(QDate(year, month, day));
                }
            }
        }
    }
}

void IMDB::extractDirectors(Movie* movie, const QString& html) const
{
    QRegularExpression rx;
    rx.setPatternOptions(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatch match;

    // 2020-12 version
    rx.setPattern(R"re(>Directors</span><div class="[^"]+"><ul class="[^"]+" role="presentation">(.*)</ul></div>)re");
    match = rx.match(html);
    if (match.hasMatch()) {
        QString directorsBlock = match.captured(1);
        rx.setPattern(R"re(href="/name/[^"]+">([^<]+)</a>)re");
        QRegularExpressionMatchIterator matches = rx.globalMatch(directorsBlock);
        QStringList directors;
        while (matches.hasNext()) {
            directors << matches.next().captured(1);
        }
        movie->setDirector(directors.join(", "));
        return;
    }

    // older version
    rx.setPattern(
        R"(<div class="txt-block" itemprop="director" itemscope itemtype="http://schema.org/Person">(.*)</div>)");
    match = rx.match(html);

    if (!match.hasMatch()) {
        // the ghost span may only exist if there are more than 2 directors
        rx.setPattern(
            R"(<div class="credit_summary_item">\n +<h4 class="inline">Directors?:</h4>(.*)(?:<span class="ghost">|</div>))");
        match = rx.match(html);
    }

    QString directorsBlock = match.captured(1);

    if (!directorsBlock.isEmpty()) {
        QStringList directors;
        rx.setPattern(R"(<a href="[^"]*"[^>]*>([^<]*)</a>)");
        QRegularExpressionMatchIterator matches = rx.globalMatch(directorsBlock);
        while (matches.hasNext()) {
            directors << matches.next().captured(1);
        }
        movie->setDirector(directors.join(", "));
    }
}

void IMDB::extractWriters(Movie* movie, const QString& html) const
{
    QRegularExpression rx;
    rx.setPatternOptions(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatch match;

    // 2020-12 version
    rx.setPattern(R"re(>Writers</span><div class="[^"]+"><ul class="[^"]+" role="presentation">(.*)</ul></div>)re");
    match = rx.match(html);
    if (match.hasMatch()) {
        QString writersBlock = match.captured(1);
        rx.setPattern(R"re(href="/name/[^"]+">([^<]+)</a>)re");
        QRegularExpressionMatchIterator matches = rx.globalMatch(writersBlock);
        QStringList writers;
        while (matches.hasNext()) {
            writers << matches.next().captured(1);
        }
        movie->setWriter(writers.join(", "));
        return;
    }

    // older version
    rx.setPattern(
        R"(<div class="txt-block" itemprop="creator" itemscope itemtype="http://schema.org/Person">(.*)</div>)");
    match = rx.match(html);
    QString writersBlock;
    if (match.hasMatch()) {
        writersBlock = match.captured(1);
    } else {
        // the ghost span may only exist if there are more than 2 writers
        rx.setPattern(
            R"(<div class="credit_summary_item">\n +<h4 class="inline">Writers?:</h4>(.*)(?:<span class="ghost">|</div>))");
        match = rx.match(html);
        if (match.hasMatch()) {
            writersBlock = match.captured(1);
        }
    }

    if (!writersBlock.isEmpty()) {
        QStringList writers;
        rx.setPattern(R"(<a href="[^"]*"[^>]*>([^<]*)</a>)");
        QRegularExpressionMatchIterator writerMatches = rx.globalMatch(writersBlock);
        while (writerMatches.hasNext()) {
            writers << writerMatches.next().captured(1);
        }
        movie->setWriter(writers.join(", "));
    }
}

void IMDB::extractCertification(Movie* movie, const QString& html) const
{
    QRegularExpression rx;
    rx.setPatternOptions(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatch match;

    // old version
    rx.setPattern(R"rx("contentRating": "([^"]*)",)rx");
    match = rx.match(html);

    if (!match.hasMatch()) {
        // 2020-12 style - Version 1 (MPAA rating)
        rx.setPattern(R"rx(>Rated ([^<]+) for)rx");
        match = rx.match(html);
    }

    if (!match.hasMatch()) {
        // 2020-12 style - Version 2 (Certificate:)
        rx.setPattern(
            R"rx(Certificate</a><div class="[^"]+"><ul class="[^"]+" role="presentation"><li role="presentation" class="[^"]+"><span class="[^"]+">([^<]+)</span>)rx");
        match = rx.match(html);
    }

    if (match.hasMatch() && match.captured(1).size() < 20) {
        movie->setCertification(helper::mapCertification(Certification(match.captured(1))));
    }
}

void IMDB::loadData(QHash<MovieScraper*, QString> ids, Movie* movie, QSet<MovieScraperInfo> infos)
{
    if (movie == nullptr) {
        return;
    }
    QString imdbId = ids.values().first();
    auto* loader =
        new mediaelch::scraper::ImdbMovieLoader(*this, imdbId, *movie, std::move(infos), m_loadAllTags, this);
    connect(loader, &ImdbMovieLoader::sigLoadDone, this, &IMDB::onLoadDone);
    loader->load();
}

void IMDB::onLoadDone(Movie& movie, mediaelch::scraper::ImdbMovieLoader* loader)
{
    loader->deleteLater();
    movie.controller()->scraperLoadDone(this);
}

void IMDB::parseAndAssignInfos(const QString& html, Movie* movie, QSet<MovieScraperInfo> infos) const
{
    using namespace std::chrono;

    QRegularExpression rx;
    rx.setPatternOptions(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatch match;

    if (infos.contains(MovieScraperInfo::Title)) {
        // "Normal" Title

        bool foundTitle = false;
        rx.setPattern(R"(<h1 class="[^"]*">([^<]*)&nbsp;)");
        match = rx.match(html);
        if (match.hasMatch()) {
            movie->setName(match.captured(1));
            foundTitle = true;
        }
        rx.setPattern(R"(<h1 itemprop="name" class="">(.*)&nbsp;<span id="titleYear">)");
        match = rx.match(html);
        if (!foundTitle && match.hasMatch()) {
            movie->setName(match.captured(1));
            foundTitle = true;
        }

        // 2020-12 style
        rx.setPattern(R"re(<title>([^<]+) \(\d{4}\))re");
        match = rx.match(html);
        if (!foundTitle && match.hasMatch()) {
            movie->setName(match.captured(1));
            foundTitle = true;
        }

        // Original Title

        rx.setPattern(R"(<div class="originalTitle">([^<]*)<span)");
        match = rx.match(html);
        if (match.hasMatch()) {
            movie->setOriginalName(match.captured(1));
        }
    }

    if (infos.contains(MovieScraperInfo::Director)) {
        extractDirectors(movie, html);
    }

    if (infos.contains(MovieScraperInfo::Writer)) {
        extractWriters(movie, html);
    }

    rx.setPattern(R"(<div class="see-more inline canwrap">\n *<h4 class="inline">Genres:</h4>(.*)</div>)");
    match = rx.match(html);
    if (infos.contains(MovieScraperInfo::Genres) && match.hasMatch()) {
        QString genres = match.captured(1);
        rx.setPattern(R"(<a href="[^"]*"[^>]*>([^<]*)</a>)");
        QRegularExpressionMatchIterator genreMatches = rx.globalMatch(genres);
        while (genreMatches.hasNext()) {
            movie->addGenre(helper::mapGenre(genreMatches.next().captured(1).trimmed()));
        }
    }

    rx.setPattern(R"(<div class="txt-block">[^<]*<h4 class="inline">Taglines:</h4>(.*)</div>)");
    match = rx.match(html);
    if (infos.contains(MovieScraperInfo::Tagline) && match.hasMatch()) {
        rx.setPattern("<span class=\"see-more inline\">.*</span>");
        const QString tagline = match.captured(1).remove(rx).trimmed();
        movie->setTagline(tagline);
    }

    rx.setPattern(R"(<div class="see-more inline canwrap">\n *<h4 class="inline">Plot Keywords:</h4>(.*)<nobr>)");
    match = rx.match(html);
    if (!m_loadAllTags && infos.contains(MovieScraperInfo::Tags) && match.hasMatch()) {
        QString tags = match.captured(1);
        rx.setPattern(R"(<span class="itemprop">([^<]*)</span>)");
        QRegularExpressionMatchIterator tagMatches = rx.globalMatch(tags);
        while (tagMatches.hasNext()) {
            movie->addTag(tagMatches.next().captured(1).trimmed());
        }
    }

    if (infos.contains(MovieScraperInfo::Released)) {
        extractReleased(movie, html);
    }

    if (infos.contains(MovieScraperInfo::Certification) && match.hasMatch()) {
        extractCertification(movie, html);
    }

    if (infos.contains(MovieScraperInfo::Runtime)) {
        rx.setPattern(R"("duration": "PT(\d+)H?(\d+)M")");
        match = rx.match(html);

        if (!match.hasMatch()) {
            // 2020-12 version
            rx.setPattern(
                R"re(>Runtime</span><div class="[^"]+"><ul class="[^"]+" role="presentation"><li role="presentation" class="[^"]+"><span class="[^"]+">(\d+)h (\d+)min</span>)re");
            match = rx.match(html);
        }

        if (match.hasMatch()) {
            if (rx.captureCount() > 1) {
                minutes runtime = hours(match.captured(1).toInt()) + minutes(match.captured(2).toInt());
                movie->setRuntime(runtime);
            } else {
                minutes runtime = minutes(match.captured(1).toInt());
                movie->setRuntime(runtime);
            }
        }
    }

    rx.setPattern(R"(<h4 class="inline">Runtime:</h4>[^<]*<time datetime="PT([0-9]+)M">)");
    match = rx.match(html);
    if (infos.contains(MovieScraperInfo::Runtime) && match.hasMatch()) {
        movie->setRuntime(minutes(match.captured(1).toInt()));
    }

    if (infos.contains(MovieScraperInfo::Overview)) {
        // Outline --------------------------

        bool foundOutline = false;
        rx.setPattern("<p itemprop=\"description\">(.*)</p>");
        match = rx.match(html);
        if (!foundOutline && match.hasMatch()) {
            QString outline = match.captured(1).remove(QRegularExpression("<[^>]*>"));
            outline = outline.remove("See full summary&nbsp;&raquo;").trimmed();
            movie->setOutline(outline);
            foundOutline = true;
        }

        rx.setPattern(R"(<div class="summary_text">(.*)</div>)");
        match = rx.match(html);
        if (!foundOutline && match.hasMatch()) {
            QString outline = match.captured(1).remove(QRegularExpression("<[^>]*>"));
            outline = outline.remove("See full summary&nbsp;&raquo;").trimmed();
            movie->setOutline(outline);
            foundOutline = true;
        }

        // 2020-12 version
        rx.setPattern(R"re(data-testid="plot-xs" class="[^"]+">(.*)</div>)re");
        match = rx.match(html);
        if (!foundOutline && match.hasMatch()) {
            QString outline = match.captured(1).remove(QRegularExpression("<[^>]*>")).trimmed();
            movie->setOutline(outline);
            foundOutline = true;
        }

        // Overview --------------------------

        bool foundOverview = false;
        rx.setPattern(R"(<h2>Storyline</h2>\n +\n +<div class="inline canwrap">\n +<p>\n +<span>(.*)</span>)");
        match = rx.match(html);
        if (!foundOverview && match.hasMatch()) {
            QString overview = match.captured(1).trimmed();
            overview.remove(QRegularExpression("<[^>]*>"));
            movie->setOverview(overview.trimmed());
            foundOverview = true;
        }

        // 2020-12 version
        rx.setPattern(R"re(data-testid="storyline-plot-summary"><div class="[^"]+"><div>(.*)<span)re");
        match = rx.match(html);
        if (!foundOverview && match.hasMatch()) {
            QString overview = match.captured(1).trimmed();
            overview.remove(QRegularExpression("<[^>]*>"));
            movie->setOverview(overview.trimmed());
            foundOverview = true;
        }
    }

    if (infos.contains(MovieScraperInfo::Rating)) {
        Rating rating;
        rating.source = "imdb";
        rating.maxRating = 10;

        // 2020-12 version
        rx.setPattern(
            R"re(<div data-testid="[^"]+rating__score" class="AggregateRating[^"]+"><span class="[^"]+">(\d+(?:\.\d+))</span>)re");
        match = rx.match(html);

        if (match.hasMatch()) {
            rating.rating = match.captured(1).trimmed().replace(",", ".").toDouble();

        } else {
            rx.setPattern("<div class=\"star-box-details\" itemtype=\"http://schema.org/AggregateRating\" itemscope "
                          "itemprop=\"aggregateRating\">(.*)</div>");
            match = rx.match(html);

            if (match.hasMatch()) {
                QString content = match.captured(1);
                rx.setPattern("<span itemprop=\"ratingValue\">(.*)</span>");
                match = rx.match(content);
                if (match.hasMatch()) {
                    rating.rating = match.captured(1).trimmed().replace(",", ".").toDouble();
                }

                rx.setPattern("<span itemprop=\"ratingCount\">(.*)</span>");
                match = rx.match(content);
                if (match.hasMatch()) {
                    rating.voteCount = match.captured(1).replace(",", "").replace(".", "").toInt();
                }
            } else {
                rx.setPattern(R"(<div class="imdbRating"[^>]*>\n +<div class="ratingValue">(.*)</div>)");
                match = rx.match(html);
                if (match.hasMatch()) {
                    QString content = match.captured(1);
                    rx.setPattern("([0-9]\\.[0-9]) based on ([0-9\\,]*) ");
                    match = rx.match(content);
                    if (match.hasMatch()) {
                        rating.rating = match.captured(1).trimmed().replace(",", ".").toDouble();
                        rating.voteCount = match.captured(2).replace(",", "").replace(".", "").toInt();
                    }
                    rx.setPattern("([0-9]\\,[0-9]) based on ([0-9\\.]*) ");
                    match = rx.match(content);
                    if (match.hasMatch()) {
                        rating.rating = match.captured(1).trimmed().replace(",", ".").toDouble();
                        rating.voteCount = match.captured(2).replace(",", "").replace(".", "").toInt();
                    }
                }
            }
        }
        movie->ratings().push_back(rating);

        // Top250 for movies
        rx.setPattern("Top Rated Movies #([0-9]+)\\n</a>");
        match = rx.match(html);
        if (match.hasMatch()) {
            movie->setTop250(match.captured(1).toInt());
        }
        // Top250 for TV shows (used by TheTvDb)
        rx.setPattern("Top Rated TV #([0-9]+)\\n</a>");
        match = rx.match(html);
        if (match.hasMatch()) {
            movie->setTop250(match.captured(1).toInt());
        }
    }

    rx.setPattern(R"(<h4 class="inline">Production Co:</h4>(.*)<span class="see-more inline">)");
    match = rx.match(html);
    if (infos.contains(MovieScraperInfo::Studios) && match.hasMatch()) {
        QString studios = match.captured(1);
        rx.setPattern(R"(<a href="/company/[^"]*"[^>]*>([^<]+)</a>)");
        QRegularExpressionMatchIterator studioMatches = rx.globalMatch(studios);
        while (studioMatches.hasNext()) {
            movie->addStudio(helper::mapStudio(studioMatches.next().captured(1).trimmed()));
        }
    }

    rx.setPattern(R"(<h4 class="inline">Country:</h4>(.*)</div>)");
    match = rx.match(html);
    if (infos.contains(MovieScraperInfo::Countries) && match.hasMatch()) {
        QString content = match.captured(1);
        rx.setPattern(R"(<a href="[^"]*"[^>]*>([^<]*)</a>)");
        QRegularExpressionMatchIterator countryMatches = rx.globalMatch(content);
        while (countryMatches.hasNext()) {
            movie->addCountry(helper::mapCountry(countryMatches.next().captured(1).trimmed()));
        }
    }
}

} // namespace scraper
} // namespace mediaelch
