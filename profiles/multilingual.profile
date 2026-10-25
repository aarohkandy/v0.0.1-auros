# AurOS Profile — Multilingual.
# Answers "make my laptop run natively in Chinese or French" with a
# profile edit rather than a rebuild.
inherit="desktop"

profile_id="multilingual"
profile_name="AurOS Multilingual"
profile_description="Ships with Chinese, French, Spanish and Arabic ready to go."

locale="fr_FR.UTF-8"
extra_locales="zh_CN.UTF-8 es_ES.UTF-8 ar_EG.UTF-8 en_US.UTF-8"
keyboard_layout="fr"
timezone="Europe/Paris"

# Client-owned GTK candidate UI and the font coverage those locales need.
# The daemon has no display connection; GTK renders its own parented popup.
input_method="fcitx"
packages_extra="fcitx5 fcitx5-frontend-gtk3 fcitx5-pinyin
                fonts-noto-cjk fonts-noto-color-emoji fonts-noto-core
                language-pack-fr language-pack-zh-hans language-pack-es
                language-pack-ar
                language-pack-gnome-fr language-pack-gnome-zh-hans
                language-pack-gnome-es language-pack-gnome-ar"
packages_exclude="snapd ubuntu-advantage-tools popularity-contest
                  ibus ibus-pinyin ibus-m17n ibus-gtk ibus-gtk3 ibus-gtk4"
