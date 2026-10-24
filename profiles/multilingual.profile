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

# Input methods and the font coverage those locales actually need.
input_method="ibus"
packages_extra="ibus ibus-pinyin ibus-m17n ibus-gtk ibus-gtk3 ibus-gtk4
                fonts-noto-cjk fonts-noto-color-emoji fonts-noto-core
                language-pack-fr language-pack-zh-hans language-pack-es
                language-pack-ar
                language-pack-gnome-fr language-pack-gnome-zh-hans
                language-pack-gnome-es language-pack-gnome-ar"
