#ifndef PACKAGEVERSION_H
#define PACKAGEVERSION_H

#include <guiddef.h>

#include <QString>
#include <QMetaType>
#include <QDir>
#include <QUrl>
#include <QStringList>
#include <QSemaphore>
#include <QXmlStreamWriter>
#include <QCryptographicHash>
#include <QJsonObject>

#include "job.h"
#include "packageversionfile.h"
#include "version.h"
#include "dependency.h"
#include "installoperation.h"
#include "detectfile.h"
#include "commandline.h"

// 30ed381d-59ea-4ca5-bd1d-5ee8ec97b2be
DEFINE_GUID(UUID_ClientID,0x30ed381dL,0x59ea,0x4ca5,0xbd,0x1d,0x5e,0xe8,0xec,0x97,0xb2,0xbe);

class InstallOperation;

/**
 * One version of a package (installed or not).
 *
 * Adding a new field:
 * - add the variable definition
 * - update toXML
 * - update toJSON
 * - update clone
 */
class PackageVersion
{
private:    
    static QSemaphore httpConnections;
    static QSemaphore installationScripts;

    /**
     * Set of PackageVersion::getStringId() for the locked package versions.
     * A locked package version cannot be installed or uninstalled.
     * Access to this data should be only done under the
     * lockedPackageVersionsMutex
     */
    static QSet<QString> lockedPackageVersions;

    /** mutex for lockedPackageVersions */
    static QMutex lockedPackageVersionsMutex;

    bool createShortcuts(const QString& dir, QString* errMsg);

    /**
     * @brief executes a script like .Npackd\Install.bat
     * @param job job for monitoring the progress
     * @param where directory where to start
     * @param path this is the name of the script like ".Npackd\Install.bat"
     *     relative to "where"
     * @param outputFile output file
     * @param env additional environment variables
     * @param printScriptOutput true = redirect the script output to the default
     *     output stream
     */
    void executeFile2(Job *job, const QString &where, const QString &path,
            const QString &outputFile,
            const QStringList &env,
            bool printScriptOutput);

    void deleteShortcuts(const QString& dir,
            Job* job, bool menu, bool desktop, bool quickLaunch);

    /**
     * @brief deletes shortcuts from a new thread
     * @param dir target directory for shortcuts
     * @param job job
     * @param menu true = delete shortcuts in the start menu
     * @param desktop true = delete shortcuts on the desktop
     * @param quickLaunch true = delete shortcuts in the quick launch bar
     */
    void deleteShortcutsRunnable(const QString& dir,
            Job* job, bool menu, bool desktop, bool quickLaunch);

    /**
     * Deletes a directory. If something cannot be deleted, it waits and
     * tries to delete the directory again. Moves the directory to .Trash if
     * it cannot be moved to the recycle bin.
     *
     * @param job progress for this task
     * @param dir this directory will be deleted
     * @param programCloseType how to close running programs. Multiple flags
     *     may be combined here using OR.
     */
    void removeDirectory(Job* job, const QString& dir, int programCloseType=0);

    void emitStatusChanged();

    QString addBasicVars(QStringList *env);
    void addDependencyVars(QStringList* vars);
public:
    /**
     * @brief string ID for the specified package version
     * @param package full package name
     * @param version package version
     * @return package + "/" + version
     */
    static QString getStringId(const QString& package, const Version& version);

    /**
     * @brief searches for the specified object in the specified list. Objects
     *     will be compared only by package and version.
     * @param pvs list of package versions
     * @param f search for this object
     * @return index of the found object or -1
     */
    static int indexOf(const QList<PackageVersion*> pvs, PackageVersion* f);

    /**
     * @param err error message will be stored here
     * @return [ownership:caller] the first found locked PackageVersion or 0
     */
    static PackageVersion* findLockedPackageVersion(QString* err);

    /**
     * @param xml <version>
     * @param err error message will be stored here
     * @param validate true = perform all available validations
     * @return created object or 0
     */
    static PackageVersion* parse(const QByteArray& xml, QString* err,
            bool validate=true);

    /**
     * @brief searches for a package version only using the package name and
     *     version number
     * @param list searching in this list
     * @param pv searching for this package version
     * @return true if the list contains the specified package version
     */
    static bool contains(const QList<PackageVersion*>& list,
            PackageVersion* pv);

    /**
     * @brief parses the command line and returns the list of chosen package
     *     versions
     * @param cl command line
     * @param err errors will be stored here
     * @return [owner:caller] list of package versions
     */
    static QList<PackageVersion *> getAddPackageVersionOptions(const CommandLine &cl, QString *err);

    /**
     * @brief parses the command line and returns the list of chosen package
     *     versions
     * @param cl command line
     * @param err errors will be stored here
     * @return [owner:caller] list of package versions
     */
    static QList<PackageVersion *> getRemovePackageVersionOptions(
            const CommandLine &cl, QString *err);

    /** package version */
    Version version;

    /** complete package name like net.sourceforge.NotepadPlusPlus */
    QString package;

    /** important files (shortcuts for these will be created in the menu) */
    QStringList importantFiles;

    /** titles for the important files */
    QStringList importantFilesTitles;

    /**
     * Text files.
     */
    QList<PackageVersionFile*> files;

    /**
     * Package detection
     */
    QList<DetectFile*> detectFiles;

    /**
     * Dependencies.
     */
    QList<Dependency*> dependencies;

    /** 0 = zip file, 1 = one file */
    int type;

    /**
     * SHA-1 or SHA-256 hash sum for the installation file or empty if not
     * defined
     */
    QString sha1;

    /** 0 = SHA-1, 1 = SHA-256 */
    QCryptographicHash::Algorithm hashSumType;

    /**
     * .zip file for downloading
     */
    QUrl download;

    /**
     * MSI GUID like {1D2C96C3-A3F3-49E7-B839-95279DED837F} or ""
     * if not available. Should be always in lower case
     */
    QString msiGUID;

    /**
     * unknown/1.0
     */
    PackageVersion();

    /**
     * package/1.0
     *
     * @param package
     */
    PackageVersion(const QString& package);

    /**
     * -
     *
     * @param package full internal package name
     * @param version package version
     */
    PackageVersion(const QString& package, const Version& version);

    virtual ~PackageVersion();

    /**
     * @brief saves the text files associated with this package version
     * @param d the files will be saved in this directory. This directory
     *     might not exist.
     * @return error message or ""
     */
    QString saveFiles(const QDir& d);

    /**
     * Locks this package version so that it cannot be installed or removed
     * by other processes.
     */
    void lock();

    /**
     * Unlocks this package version so that it can be installed or removed
     * again.
     */
    void unlock();

    /**
     * @return true if this package version is locked and cannot be installed
     *     or removed
     */
    bool isLocked() const;

    /**
     * @return installation path or "" if the package is not installed
     */
    QString getPath() const;

    /**
     * Changes the installation path for this package. This method should only
     * be used if the package was detected.
     *
     * @param path installation path
     * @return error message
     */
    QString setPath(const QString& path);

    /**
     * Renames the directory for this package to a temporary name and then
     * renames it back.
     *
     * @return true if the renaming was OK (the directory is not locked)
     */
    bool isDirectoryLocked();

    /**
     * Downloads the package and computes its SHA1.
     *
     * @return SHA1
     */
    QString downloadAndComputeSHA1(Job* job);

    /**
     * Returns the extension of the package file (quessing from the URL).
     *
     * @return e.g. ".exe" or ".zip". Never returns an empty string
     */
    QString getFileExtension();

    /**
     * Plans installation of this package and all the dependencies recursively.
     *
     * @param installed [ownership:caller] list of installed packages.
     *     This list should be
     *     consulted instead of .installed() and will be updated and contains
     *     all installed package versions after the process
     * @param op [ownership:caller] necessary operations will be appended here.
     *     The existing
     *     elements will not be modified in any way.
     * @param avoid [ownership:caller] list of package versions that cannot be
     *     installed. The list
     *     will be changed by this method. Normally this is an empty list and
     *     objects will be added to it on different recursion levels.
     * @param where target directory for the installation or "" if the
     *     directory should be chosen automatically
     * @return error message or ""
     */
    QString planInstallation(QList<PackageVersion*>& installed,
            QList<InstallOperation*>& ops, QList<PackageVersion*>& avoid,
            const QString &where="");

    /**
     * Plans un-installation of this package and all the dependent recursively.
     *
     * @param installed list of installed packages. This list should be
     *     consulted instead of .installed() and will be updated and contains
     *     all installed package versions after the process. The list will also
     *     be updated to reflect packages "uninstalled" by this method
     * @param op necessary operations will be added here. The existing
     *     elements will not be modified in any way.
     * @return error message or ""
     */
    QString planUninstallation(QList<PackageVersion*>& installed,
            QList<InstallOperation*>& ops);

    /**
     * @param includeFullPackageName true = full package name will be added
     * @return package title
     */
    QString getPackageTitle(bool includeFullPackageName=false) const;

    /**
     * @return only the last part of the package name (without a dot)
     */
    QString getShortPackageName();

    /**
     * @param includeFullPackageName true = the full package name will be added
     * @return human readable title for this package version
     */
    QString toString(bool includeFullPackageName=false);

    /**
     * @return true if this package version is installed
     */
    bool installed() const;

    /**
     * @return a non-existing directory where this package would normally be
     *     installed (e.g. C:\Program Files\My Prog 2.3.2)
     */
    QString getPreferredInstallationDirectory();

    /**
     * @return a maybe existing directory where this package would normally
     *     installed (e.g. C:\Program Files\My_Prog)
     */
    QString getIdealInstallationDirectory();

    /**
     * @return a maybe existing directory where this package would normally
     *     installed as secondary location including the version number
     *     (e.g. C:\Program Files\My_Prog-12.2.3)
     */
    QString getSecondaryInstallationDirectory();

    /**
     * Installs this package without dependencies. The directory should already
     * exist and be prepared by this->download(...)
     *
     * @param job job for this method
     * @param where target directory
     * @param binary relative file name of the downloaded binary
     *     or "" for packages of type "zip"
     * @param printScriptOutput true = redirect the script output to the default
     *     output stream
     * @param programCloseType how to close running programs. Multiple flags
     *     may be combined here using OR.
     */
    void install(Job* job, const QString& where, const QString &binary,
            bool printScriptOutput, int programCloseType=0);

    /**
     * Downloads the package binary, checks its hash sum, checks the binary for
     * viruses, unpacks it in case of a .zip file, stores the text files.
     *
     * @param job job for this method
     * @param where a non-existing directory for the package
     * @param interactive true = allow the interaction with the user
     * @return the full name of the downloaded file or "" for packages of
     *     type "zip"
     */
    QString download_(Job* job, const QString& where, bool interactive=true);

    /**
     * Uninstalls this package version.
     *
     * @param job job for this method
     * @param printScriptOutput true = redirect the script output to the default
     *     output stream
     * @param programCloseType how to close running programs. Multiple flags
     *     may be combined here using OR.
     */
    void uninstall(Job* job, bool printScriptOutput, int programCloseType=0);

    /**
     * @return status like "locked, installed"
     */
    QString getStatus() const;

    /**
     * Stores this object as XML <version>.
     *
     * @param w output
     */
    void toXML(QXmlStreamWriter* w) const;

    /**
     * Stores this object as JSON
     *
     * @param w output
     */
    void toJSON(QJsonObject &w) const;

    /**
     * @return a copy
     */
    PackageVersion* clone() const;

    /**
     * @return true if this package is in c:\Windows or one of the nested
     *     directories
     */
    bool isInWindowsDir() const;

    /**
     * @brief string that can be used to identify this package and version
     * @return "package/version"
     */
    QString getStringId() const;

    /**
     * @brief transfers all data from another object into this
     * @param pv another package version
     */
    void fillFrom(PackageVersion* pv);

    /**
     * @brief searches for a definition of a text file
     * @param path file path (case-insensitive)
     * @return [ownership:this] found file or 0
     */
    PackageVersionFile *findFile(const QString &path) const;

    /**
     * @brief stops this package version if it is running. This either executes
     *     .Npackd\Stop.bat or closes the running applications otherwise.
     * @param [ownership:callser] job
     * @param programCloseType how to close running programs. Multiple flags
     *     may be combined here using OR.
     * @param printScriptOutput true = redirect the script output to the default
     *     output stream
     */
    void stop(Job *job, int programCloseType, bool printScriptOutput);
};

Q_DECLARE_METATYPE(PackageVersion);

inline QString PackageVersion::getStringId(const QString& package,
    const Version& version)
{
    QString r(package);
    r.append('/');
    Version v = version;
    v.normalize();
    r.append(v.getVersionString());
    return r;
}

#endif // PACKAGEVERSION_H
