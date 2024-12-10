#pragma once
///@file

#include "types.hh"
#include "hash.hh"
#include "canon-path.hh"
#include "json-impls.hh"
#include "attrs.hh"
#include "url.hh"

#include <memory>
#include <nlohmann/json_fwd.hpp>

#include "ref.hh"

namespace nix { class Store; class StorePath; struct SourceAccessor; }

namespace nix::fetchers {

struct InputScheme;

struct Settings;

/**
 * The `Input` object is generated by a specific fetcher, based on
 * user-supplied information, and contains
 * the information that the specific fetcher needs to perform the
 * actual fetch.  The Input object is most commonly created via the
 * `fromURL()` or `fromAttrs()` static functions.
 */
struct Input
{
    friend struct InputScheme;

    const Settings * settings;

    Input(const Settings & settings)
        : settings{&settings}
    { }

    std::shared_ptr<InputScheme> scheme; // note: can be null
    Attrs attrs;

    /**
     * path of the parent of this input, used for relative path resolution
     */
    std::optional<Path> parent;

public:
    /**
     * Create an `Input` from a URL.
     *
     * The URL indicate which sort of fetcher, and provides information to that fetcher.
     */
    static Input fromURL(
        const Settings & settings,
        const std::string & url, bool requireTree = true);

    static Input fromURL(
        const Settings & settings,
        const ParsedURL & url, bool requireTree = true);

    /**
     * Create an `Input` from a an `Attrs`.
     *
     * The URL indicate which sort of fetcher, and provides information to that fetcher.
     */
    static Input fromAttrs(
        const Settings & settings,
        Attrs && attrs);

    ParsedURL toURL() const;

    std::string toURLString(const std::map<std::string, std::string> & extraQuery = {}) const;

    std::string to_string() const;

    Attrs toAttrs() const;

    /**
     * Return whether this is a "direct" input, that is, not
     * one that goes through a registry.
     */
    bool isDirect() const;

    /**
     * Return whether this is a "locked" input, that is, it has
     * attributes like a Git revision or NAR hash that uniquely
     * identify its contents.
     */
    bool isLocked() const;

    /**
     * Return whether this is a "final" input, meaning that fetching
     * it will not add, remove or change any attributes. (See
     * `checkLocks()` for the semantics.) Only "final" inputs can be
     * substituted from a binary cache.
     *
     * The "final" state is denoted by the presence of an attribute
     * `__final = true`. This attribute is currently undocumented and
     * for internal use only.
     */
    bool isFinal() const;

    bool operator ==(const Input & other) const noexcept;

    auto operator <=>(const Input & other) const
    {
        return attrs <=> other.attrs;
    }

    bool contains(const Input & other) const;

    /**
     * Fetch the entire input into the Nix store, returning the
     * location in the Nix store and the locked input.
     */
    std::pair<StorePath, Input> fetchToStore(ref<Store> store) const;

    /**
     * Check the locking attributes in `result` against
     * `specified`. E.g. if `specified` has a `rev` attribute, then
     * `result` must have the same `rev` attribute. Throw an exception
     * if there is a mismatch.
     *
     * If `specified` is marked final (i.e. has the `__final`
     * attribute), then the intersection of attributes in `specified`
     * and `result` must be equal, and `final.attrs` is set to
     * `specified.attrs` (i.e. we discard any new attributes).
     */
    static void checkLocks(Input specified, Input & result);

    /**
     * Return a `SourceAccessor` that allows access to files in the
     * input without copying it to the store. Also return a possibly
     * unlocked input.
     */
    std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store) const;

private:

    std::pair<ref<SourceAccessor>, Input> getAccessorUnchecked(ref<Store> store) const;

public:

    Input applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    void clone(const Path & destDir) const;

    std::optional<Path> getSourcePath() const;

    /**
     * Write a file to this input, for input types that support
     * writing. Optionally commit the change (for e.g. Git inputs).
     */
    void putFile(
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const;

    std::string getName() const;

    StorePath computeStorePath(Store & store) const;

    // Convenience functions for common attributes.
    std::string getType() const;
    std::optional<Hash> getNarHash() const;
    std::optional<std::string> getRef() const;
    std::optional<Hash> getRev() const;
    std::optional<uint64_t> getRevCount() const;
    std::optional<time_t> getLastModified() const;

    /**
     * For locked inputs, return a string that uniquely specifies the
     * content of the input (typically a commit hash or content hash).
     *
     * Only known-equivalent inputs should return the same fingerprint.
     *
     * This is not a stable identifier between Nix versions, but not guaranteed to change either.
     */
    std::optional<std::string> getFingerprint(ref<Store> store) const;
};

/**
 * The `InputScheme` represents a type of fetcher.  Each fetcher
 * registers with nix at startup time.  When processing an `Input`,
 * each scheme is given an opportunity to "recognize" that
 * input from the user-provided url or attributes
 * and return an `Input` object to represent the input if it is
 * recognized.  The `Input` object contains the information the fetcher
 * needs to actually perform the `fetch()` when called.
 */
struct InputScheme
{
    virtual ~InputScheme()
    { }

    virtual std::optional<Input> inputFromURL(
        const Settings & settings,
        const ParsedURL & url, bool requireTree) const = 0;

    virtual std::optional<Input> inputFromAttrs(
        const Settings & settings,
        const Attrs & attrs) const = 0;

    /**
     * What is the name of the scheme?
     *
     * The `type` attribute is used to select which input scheme is
     * used, and then the other fields are forwarded to that input
     * scheme.
     */
    virtual std::string_view schemeName() const = 0;

    /**
     * Allowed attributes in an attribute set that is converted to an
     * input.
     *
     * `type` is not included from this set, because the `type` field is
      parsed first to choose which scheme; `type` is always required.
     */
    virtual StringSet allowedAttrs() const = 0;

    virtual ParsedURL toURL(const Input & input) const;

    virtual Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const;

    virtual void clone(const Input & input, const Path & destDir) const;

    virtual std::optional<Path> getSourcePath(const Input & input) const;

    virtual void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const;

    virtual std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store, const Input & input) const = 0;

    /**
     * Is this `InputScheme` part of an experimental feature?
     */
    virtual std::optional<ExperimentalFeature> experimentalFeature() const;

    virtual bool isDirect(const Input & input) const
    { return true; }

    virtual std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const
    { return std::nullopt; }

    virtual bool isLocked(const Input & input) const
    { return false; }
};

void registerInputScheme(std::shared_ptr<InputScheme> && fetcher);

nlohmann::json dumpRegisterInputSchemeInfo();

struct PublicKey
{
    std::string type = "ssh-ed25519";
    std::string key;

    auto operator <=>(const PublicKey &) const = default;
};

std::string publicKeys_to_string(const std::vector<PublicKey>&);

}

JSON_IMPL(fetchers::PublicKey)
