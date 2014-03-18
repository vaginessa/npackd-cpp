#include <windows.h>
#include <shlobj.h>
#include <msi.h>

#include <QTemporaryFile>
#include <qdom.h>
#include <QDebug>
#include <QXmlSimpleReader>
#include <QXmlInputSource>

#include "downloader.h"
#include "repository.h"
#include "downloader.h"
#include "packageversionfile.h"
#include "wpmutils.h"
#include "version.h"
#include "windowsregistry.h"
#include "xmlutils.h"
#include "wpmutils.h"
#include "installedpackages.h"
#include "dbrepository.h"
#include "repositoryxmlhandler.h"

Repository Repository::def;
QMutex Repository::mutex;

Repository::Repository(): AbstractRepository()
{
}

static bool packageVersionLessThan2(const PackageVersion* a,
        const PackageVersion* b) {
    int r = a->package.compare(b->package);
    if (r == 0) {
        r = a->version.compare(b->version);
    }

    return r < 0;
}

QList<PackageVersion*> Repository::getPackageVersions(const QString& package)
        const
{
    QList<PackageVersion*> ret = this->package2versions.values(package);

    qSort(ret.begin(), ret.end(), packageVersionLessThan2);

    return ret;
}

QList<PackageVersion*> Repository::getPackageVersions_(const QString& package,
        QString *err) const
{
    *err = "";

    QList<PackageVersion*> ret = this->package2versions.values(package);

    qSort(ret.begin(), ret.end(), packageVersionLessThan2);

    for (int i = 0; i < ret.count(); i++) {
        ret[i] = ret.at(i)->clone();
    }

    return ret;
}

Repository::~Repository()
{
    qDeleteAll(this->packages);
    qDeleteAll(this->packageVersions);
    qDeleteAll(this->licenses);
}

PackageVersion* Repository::findNewestInstallablePackageVersion(
        const QString &package)
{
    PackageVersion* r = 0;

    QList<PackageVersion*> pvs = this->getPackageVersions(package);
    for (int i = 0; i < pvs.count(); i++) {
        PackageVersion* p = pvs.at(i);
        if (r == 0 || p->version.compare(r->version) > 0) {
            if (p->download.isValid())
                r = p;
        }
    }
    return r;
}

PackageVersion* Repository::createPackageVersion(QDomElement* e, QString* err)
{
    return PackageVersion::parse(e, err);
}

Package* Repository::createPackage(QDomElement* e, QString* err)
{
    *err = "";

    QString name = e->attribute("name").trimmed();
    *err = WPMUtils::validateFullPackageName(name);
    if (!err->isEmpty()) {
        err->prepend(QObject::tr("Error in attribute 'name' in <package>: "));
    }

    Package* a = new Package(name, name);

    if (err->isEmpty()) {
        a->title = XMLUtils::getTagContent(*e, "title");
        a->url = XMLUtils::getTagContent(*e, "url");
        a->description = XMLUtils::getTagContent(*e, "description");
    }

    if (err->isEmpty()) {
        a->icon = XMLUtils::getTagContent(*e, "icon");
        if (!a->icon.isEmpty()) {
            QUrl u(a->icon);
            if (!u.isValid() || u.isRelative() ||
                    !(u.scheme() == "http" || u.scheme() == "https")) {
                err->append(QString(
                        QObject::tr("Invalid icon URL for %1: %2")).
                        arg(a->title).arg(a->icon));
            }
        }
    }

    if (err->isEmpty()) {
        a->license = XMLUtils::getTagContent(*e, "license");
    }

    if (err->isEmpty()) {
        QDomNodeList categories = e->elementsByTagName("category");
        for (int i = 0; i < categories.count(); i++) {
            QDomElement e = categories.at(i).toElement();
            QString c = checkCategory(e.text().trimmed(), err);
            if (!err->isEmpty()) {
                *err = QObject::tr("Error in category tag for %1: %2").
                        arg(a->title).arg(*err);
                break;
            }

            if (a->categories.contains(c)) {
                *err = QObject::tr("More than one <category> %1 for %2").
                        arg(c).arg(a->title);
            }

            a->categories.append(c);
        }
    }

    if (err->isEmpty())
        return a;
    else {
        delete a;
        return 0;
    }
}

License* Repository::createLicense(QDomElement* e, QString* error)
{
    License* a = 0;
    *error = "";

    QString name = e->attribute("name");
    *error = WPMUtils::validateFullPackageName(name);
    if (!error->isEmpty()) {
        error->prepend(QObject::tr("Error in attribute 'name' in <package>: "));
    }

    if (error->isEmpty()) {
        a = new License(name, name);
        QDomNodeList nl = e->elementsByTagName("title");
        if (nl.count() != 0)
            a->title = nl.at(0).firstChild().nodeValue();
        nl = e->elementsByTagName("url");
        if (nl.count() != 0)
            a->url = nl.at(0).firstChild().nodeValue();
        nl = e->elementsByTagName("description");
        if (nl.count() != 0)
            a->description = nl.at(0).firstChild().nodeValue();
    }

    if (!error->isEmpty()) {
        delete a;
        a = 0;
    }

    return a;
}

License* Repository::findLicense(const QString& name)
{
    for (int i = 0; i < this->licenses.count(); i++) {
        if (this->licenses.at(i)->name == name)
            return this->licenses.at(i);
    }
    return 0;
}

Package* Repository::findPackage(const QString& name)
{
    for (int i = 0; i < this->packages.count(); i++) {
        if (this->packages.at(i)->name == name)
            return this->packages.at(i);
    }
    return 0;
}

QString Repository::writeTo(const QString& filename) const
{
    QString r;

    QDomDocument doc;
    QDomElement root = doc.createElement("root");
    doc.appendChild(root);

    XMLUtils::addTextTag(root, "spec-version", "3");

    for (int i = 0; i < this->packages.count(); i++) {
        Package* p = packages.at(i);
        QDomElement package = doc.createElement("package");
        p->saveTo(package);
        root.appendChild(package);
    }

    for (int i = 0; i < this->packageVersions.count(); i++) {
        PackageVersion* pv = this->packageVersions.at(i);
        QDomElement version = doc.createElement("version");
        root.appendChild(version);
        pv->toXML(&version);
    }

    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream s(&file);
        doc.save(s, 4);
    } else {
        r = QString(QObject::tr("Cannot open %1 for writing")).arg(filename);
    }

    return "";
}

PackageVersion* Repository::findPackageVersion(const QString& package,
        const Version& version)
{
    PackageVersion* r = 0;

    QList<PackageVersion*> pvs = this->getPackageVersions(package);
    for (int i = 0; i < pvs.count(); i++) {
        PackageVersion* pv = pvs.at(i);
        if (pv->version.compare(version) == 0) {
            r = pv;
            break;
        }
    }
    return r;
}

QString Repository::checkSpecVersion(const QString &specVersion)
{
    QString err;

    Version specVersion_;
    if (!specVersion_.setVersion(specVersion)) {
        err = QString(
                QObject::tr("Invalid repository specification version: %1")).
                arg(specVersion);
    } else {
        if (specVersion_.compare(Version(4, 0)) >= 0)
            err = QString(
                    QObject::tr("Incompatible repository specification version: %1.") + " \n" +
                    QObject::tr("Plese download a newer version of Npackd from http://code.google.com/p/windows-package-manager/")).
                    arg(specVersion);
    }

    return err;
}

QString Repository::checkCategory(const QString &category, QString *err)
{
    *err = "";

    QString c = category.trimmed();

    if (c.isEmpty()) {
        *err = QObject::tr("Empty category tag");
    }

    QStringList parts;
    if (err->isEmpty()) {
        parts = c.split('/', QString::KeepEmptyParts);
        if (parts.count() == 0) {
            *err = QObject::tr("Empty category tag");
        }
    }

    if (err->isEmpty()) {
        for (int j = 0; j < parts.count(); j++) {
            QString part = parts.at(j).trimmed();
            if (part.isEmpty()) {
                *err = QObject::tr("Empty sub-category");
                goto out;
            }
            parts[j] = part;
        }
out:;
    }

    return parts.join("/");
}

Package *Repository::findPackage_(const QString &name)
{
    Package* p = findPackage(name);
    if (p)
        p = p->clone();
    return p;
}

/* TODO
void Repository::load(Job* job, bool useCache)
{
    qDeleteAll(this->packages);
    this->packages.clear();
    this->package2versions.clear();
    qDeleteAll(this->packageVersions);
    this->packageVersions.clear();

    QString err;
    QList<QUrl*> urls = AbstractRepository::getRepositoryURLs(&err);
    if (urls.count() > 0) {
        for (int i = 0; i < urls.count(); i++) {
            job->setHint(QString(
                    QObject::tr("Repository %1 of %2")).arg(i + 1).
                         arg(urls.count()));
            Job* s = job->newSubJob(1.0 / urls.count());
            loadOne(urls.at(i), s, useCache);
            if (!s->getErrorMessage().isEmpty()) {
                job->setErrorMessage(QString(
                        QObject::tr("Error loading the repository %1: %2")).arg(
                        urls.at(i)->toString()).arg(
                        s->getErrorMessage()));
                delete s;
                break;
            }
            delete s;

            if (job->isCancelled())
                break;
        }
    } else {
        job->setErrorMessage(QObject::tr("No repositories defined"));
        job->setProgress(1);
    }

    // qDebug() << "Repository::load.3";

    qDeleteAll(urls);
    urls.clear();

    job->complete();
}
*/

/*
 *TODO
void Repository::loadOne(QUrl* url, Job* job, bool useCache) {
    job->setHint(QObject::tr("Downloading"));

    QTemporaryFile* f = 0;
    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        Job* djob = job->newSubJob(0.90);
        f = Downloader::download(djob, *url, 0, useCache);
        if (!djob->getErrorMessage().isEmpty())
            job->setErrorMessage(QString(
                    QObject::tr("Download failed: %2")).
                    arg(djob->getErrorMessage()));
        delete djob;
    }

    if (job->shouldProceed(QObject::tr("Parsing XML"))) {
        RepositoryXMLHandler handler(this);
        QXmlSimpleReader reader;
        reader.setContentHandler(&handler);
        reader.setErrorHandler(&handler);
        QXmlInputSource inputSource(f);
        if (!reader.parse(inputSource))
            job->setErrorMessage(handler.errorString());
    }

    delete f;

    job->complete();
}
*/

void Repository::loadOne(const QString& filename, Job* job)
{
    QFile f(filename);
    if (job->shouldProceed(QObject::tr("Opening file"))) {
        if (!f.open(QIODevice::ReadOnly))
            job->setErrorMessage(QObject::tr("Cannot open the file"));
        else
            job->setProgress(0.1);
    }


    QDomDocument doc;
    if (job->shouldProceed(QObject::tr("Parsing XML"))) {
        int errorLine, errorColumn;
        QString err;
        if (!doc.setContent(&f, false, &err, &errorLine, &errorColumn))
            job->setErrorMessage(QString(
                    QObject::tr("XML parsing failed at line %1, column %2: %3")).
                    arg(errorLine).arg(errorColumn).arg(err));
        else
            job->setProgress(0.6);
    }

    if (job->shouldProceed(QObject::tr("Analyzing the content"))) {
        Job* sub = job->newSubJob(0.4);
        loadOne(&doc, sub);
        if (sub->getErrorMessage().isEmpty())
            job->setErrorMessage(sub->getErrorMessage());
        delete sub;
    }

    job->complete();
}

void Repository::loadOne(QDomDocument* doc, Job* job)
{
    QDomElement root;
    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        root = doc->documentElement();
        QDomNodeList nl = root.elementsByTagName("spec-version");
        if (nl.count() != 0) {
            QString err = checkSpecVersion(nl.at(0).firstChild().nodeValue());
            if (!err.isEmpty())
                job->setErrorMessage(err);
            else
                job->setProgress(0.01);
        } else {
            job->setProgress(0.01);
        }
    }

    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        for (QDomNode n = root.firstChild(); !n.isNull();
                n = n.nextSibling()) {
            if (n.isElement()) {
                QDomElement e = n.toElement();
                if (e.nodeName() == "version") {
                    QString err;
                    PackageVersion* pv = createPackageVersion(&e, &err);
                    if (pv) {
                        if (this->findPackageVersion(pv->package, pv->version))
                            delete pv;
                        else {
                            this->packageVersions.append(pv);
                            this->package2versions.insert(pv->package, pv);
                        }
                    } else {
                        job->setErrorMessage(err);
                        break;
                    }
                } else if (e.nodeName() == "package") {
                    QString err;
                    Package* p = createPackage(&e, &err);
                    if (p) {
                        if (this->findPackage(p->name))
                            delete p;
                        else
                            this->packages.append(p);
                    } else {
                        job->setErrorMessage(err);
                        break;
                    }
                } else if (e.nodeName() == "license") {
                    QString err;
                    License* p = createLicense(&e, &err);
                    if (p) {
                        if (this->findLicense(p->name))
                            delete p;
                        else
                            this->licenses.append(p);
                    } else {
                        job->setErrorMessage(err);
                        break;
                    }
                }
            }
        }
        job->setProgress(1);
    }

    job->complete();
}

QString Repository::saveLicense(License *p)
{
    License* fp = findLicense(p->name);
    if (!fp) {
        fp = new License(p->name, p->title);
        this->licenses.append(fp);
    }
    fp->title = p->title;
    fp->url = p->url;
    fp->description = p->description;

    return "";
}

QString Repository::savePackage(Package *p)
{
    Package* fp = findPackage(p->name);
    if (!fp) {
        fp = new Package(p->name, p->title);
        this->packages.append(fp);
    }
    fp->title = p->title;
    fp->url = p->url;
    fp->icon = p->icon;
    fp->description = p->description;
    fp->license = p->license;
    fp->categories = p->categories;

    return "";
}

QString Repository::savePackageVersion(PackageVersion *p)
{
    PackageVersion* fp = findPackageVersion(p->package, p->version);
    if (!fp) {
        fp = new PackageVersion(p->package);
        fp->version = p->version;
        this->packageVersions.append(fp);
        this->package2versions.insert(p->package, fp);
    }
    fp->fillFrom(p);

    return "";
}

PackageVersion *Repository::findPackageVersionByMSIGUID_(
        const QString &guid, QString* err) const
{
    *err = "";

    PackageVersion* r = 0;
    for (int i = 0; i < this->packageVersions.count(); i++) {
        PackageVersion* pv = (PackageVersion*) this->packageVersions.at(i);
        if (pv->msiGUID == guid) {
            r = pv;
            break;
        }
    }

    if (r)
        r = r->clone();

    return r;
}

PackageVersion *Repository::findPackageVersion_(const QString &package,
        const Version &version, QString *err)
{
    *err = "";

    PackageVersion* pv = findPackageVersion(package, version);
    if (pv)
        pv = pv->clone();
    return pv;
}

License *Repository::findLicense_(const QString &name, QString* err)
{
    *err = "";
    License* r = findLicense(name);
    if (r)
        r = r->clone();
    return r;
}

QString Repository::clear()
{
    qDeleteAll(this->packages);
    this->packages.clear();

    qDeleteAll(this->packageVersions);
    this->packageVersions.clear();

    this->package2versions.clear();

    qDeleteAll(this->licenses);
    this->licenses.clear();

    return "";
}

QList<Package*> Repository::findPackagesByShortName(const QString &name)
{
    QString suffix = "." + name;
    QList<Package*> r;
    for (int i = 0; i < this->packages.count(); i++) {
        QString n = this->packages.at(i)->name;
        if (n.endsWith(suffix) || n == name) {
            r.append(this->packages.at(i)->clone());
        }
    }

    return r;
}
