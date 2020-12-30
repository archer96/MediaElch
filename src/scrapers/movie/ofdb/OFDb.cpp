#include "scrapers/movie/ofdb/OFDb.h"

#include "data/Storage.h"
#include "globals/Globals.h"
#include "globals/Helper.h"
#include "network/NetworkRequest.h"
#include "scrapers/movie/ofdb/OfdbSearchJob.h"
#include "settings/Settings.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QWidget>
#include <QXmlStreamReader>

namespace mediaelch {
namespace scraper {

/// \brief OFDb scraper. Uses http://ofdbgw.metawave.ch directly because ttp://www.ofdbgw.org
/// is as of 2019-02-23 down.
OFDb::OFDb(QObject* parent) : MovieScraper(parent)
{
    m_meta.identifier = ID;
    m_meta.name = "OFDb";
    m_meta.description = tr("OFDb is a German online movie database.");
    m_meta.website = "https://ssl.ofdb.de/";
    m_meta.termsOfService = "https://ssl.ofdb.de/view.php?page=info#rechtliches";
    m_meta.privacyPolicy = "https://ssl.ofdb.de/view.php?page=info#datenschutz";
    m_meta.help = "https://www.gemeinschaftsforum.com/forum/";
    m_meta.supportedDetails = {MovieScraperInfo::Title,
        MovieScraperInfo::Released,
        MovieScraperInfo::Poster,
        MovieScraperInfo::Rating,
        MovieScraperInfo::Genres,
        MovieScraperInfo::Actors,
        MovieScraperInfo::Countries,
        MovieScraperInfo::Overview};
    m_meta.supportedLanguages = {"de"};
    m_meta.defaultLocale = "de";
    m_meta.isAdult = false;
}

const MovieScraper::ScraperMeta& OFDb::meta() const
{
    return m_meta;
}

void OFDb::initialize()
{
    // no-op
    // OFDb requires no initialization.
}

bool OFDb::isInitialized() const
{
    // OFDb requires no initialization.
    return true;
}

MovieSearchJob* OFDb::search(MovieSearchJob::Config config)
{
    return new OfdbSearchJob(m_api, std::move(config), this);
}

bool OFDb::hasSettings() const
{
    return false;
}

void OFDb::loadSettings(ScraperSettings& settings)
{
    Q_UNUSED(settings);
}

void OFDb::saveSettings(ScraperSettings& settings)
{
    Q_UNUSED(settings);
}

/**
 * \brief Just returns a pointer to the scrapers network access manager
 * \return Network Access Manager
 */
mediaelch::network::NetworkManager* OFDb::network()
{
    return &m_network;
}

QSet<MovieScraperInfo> OFDb::scraperNativelySupports()
{
    return m_meta.supportedDetails;
}

void OFDb::changeLanguage(mediaelch::Locale /*locale*/)
{
    // no-op: Only one language is supported and it is hard-coded.
}

/**
 * \brief Starts network requests to download infos from OFDb
 * \param ids OFDb movie ID
 * \param movie Movie object
 * \param infos List of infos to load
 * \see OFDb::loadFinished
 */
void OFDb::loadData(QHash<MovieScraper*, mediaelch::scraper::MovieIdentifier> ids,
    Movie* movie,
    QSet<MovieScraperInfo> infos)
{
    m_api.loadMovie(ids.values().first().str(), [this, movie, infos](QString html, ScraperError error) {
        if (!error.hasError()) {
            movie->clear(infos);
            parseAndAssignInfos(html, movie, infos);
        }

        movie->controller()->scraperLoadDone(this, error);
    });
}

/**
 * \brief Parses HTML data and assigns it to the given movie object
 * \param data HTML data
 * \param movie Movie object
 * \param infos List of infos to load
 */
void OFDb::parseAndAssignInfos(QString data, Movie* movie, QSet<MovieScraperInfo> infos)
{
    QXmlStreamReader xml(data);

    if (!xml.readNextStartElement()) {
        qWarning() << "[OFDb] XML has unexpected structure; couldn't read root element";
        return;
    }

    while (xml.readNextStartElement()) {
        if (xml.name() != "resultat") {
            xml.skipCurrentElement();
        } else {
            break;
        }
    }

    while (xml.readNextStartElement()) {
        if (infos.contains(MovieScraperInfo::Title) && xml.name() == "titel") {
            movie->setName(xml.readElementText());
        } else if (infos.contains(MovieScraperInfo::Released) && xml.name() == "jahr") {
            movie->setReleased(QDate::fromString(xml.readElementText(), "yyyy"));
        } else if (infos.contains(MovieScraperInfo::Poster) && xml.name() == "bild") {
            QString url = xml.readElementText();
            Poster p;
            p.originalUrl = QUrl(url);
            p.thumbUrl = QUrl(url);
            movie->images().addPoster(p);
        } else if (infos.contains(MovieScraperInfo::Rating) && xml.name() == "bewertung") {
            while (xml.readNextStartElement()) {
                if (xml.name() == "note") {
                    Rating rating;
                    rating.source = "OFDb";
                    rating.rating = xml.readElementText().toDouble();
                    if (movie->ratings().isEmpty()) {
                        movie->ratings().push_back(rating);
                    } else {
                        movie->ratings().back() = rating;
                    }

                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (infos.contains(MovieScraperInfo::Genres) && xml.name() == "genre") {
            while (xml.readNextStartElement()) {
                if (xml.name() == "titel") {
                    movie->addGenre(helper::mapGenre(xml.readElementText()));
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (infos.contains(MovieScraperInfo::Actors) && xml.name() == "besetzung") {
            // clear actors
            movie->setActors({});

            while (xml.readNextStartElement()) {
                if (xml.name() != "person") {
                    xml.skipCurrentElement();
                } else {
                    Actor actor;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == "name") {
                            actor.name = xml.readElementText();
                        } else if (xml.name() == "rolle") {
                            actor.role = xml.readElementText();
                        } else {
                            xml.skipCurrentElement();
                        }
                    }
                    movie->addActor(actor);
                }
            }
        } else if (infos.contains(MovieScraperInfo::Countries) && xml.name() == "produktionsland") {
            while (xml.readNextStartElement()) {
                if (xml.name() == "name") {
                    movie->addCountry(helper::mapCountry(xml.readElementText()));
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (infos.contains(MovieScraperInfo::Title) && xml.name() == "alternativ") {
            movie->setOriginalName(xml.readElementText());
        } else if (infos.contains(MovieScraperInfo::Overview) && xml.name() == "beschreibung") {
            movie->setOverview(xml.readElementText());
            if (Settings::instance()->usePlotForOutline()) {
                movie->setOutline(xml.readElementText());
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

QWidget* OFDb::settingsWidget()
{
    return nullptr;
}

} // namespace scraper
} // namespace mediaelch
