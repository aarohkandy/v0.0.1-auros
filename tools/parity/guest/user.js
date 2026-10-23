// NOT PART OF AUROS: the test's Firefox profile. Only what a test page needs that a person
// would click: allow the camera and microphone, and play a film when asked.
// (The image's /etc/firefox/policies still apply on top of this.)
user_pref("media.navigator.permission.disabled", true);
user_pref("media.peerconnection.ice.obfuscate_host_addresses", false);
user_pref("media.peerconnection.ice.loopback", true);
user_pref("media.autoplay.default", 0);
user_pref("media.autoplay.blocking_policy", 0);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.aboutwelcome.enabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
