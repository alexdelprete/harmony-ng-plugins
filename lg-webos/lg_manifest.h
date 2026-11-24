/* The manifest a webOS TV asks for in its "register" request, as LG's own remote app sends it.
 * Generated from aiowebostv 1.1.1 handshake.py (Apache-2.0): REGISTRATION_PAYLOAD (signed), and the
 * unsigned variant newer firmware takes when it rejects the signature (no "signatures", the two
 * permission lists merged and sorted). JSON with non-ASCII names as \u escapes. */
#ifndef LG_MANIFEST_H
#define LG_MANIFEST_H

#define LG_MANIFEST_SIGNED \
    "{\"appVersion\":\"1.1\",\"manifestVersion\":1,\"permissions\":[\"LAUNCH\",\"LAUNCH_WEBAPP\",\"" \
    "APP_TO_APP\",\"CLOSE\",\"TEST_OPEN\",\"TEST_PROTECTED\",\"CONTROL_AUDIO\",\"CONTROL_DISPLAY\"" \
    ",\"CONTROL_INPUT_JOYSTICK\",\"CONTROL_INPUT_MEDIA_RECORDING\",\"CONTROL_INPUT_MEDIA_PLAYBACK\"" \
    ",\"CONTROL_INPUT_TV\",\"CONTROL_POWER\",\"CONTROL_TV_SCREEN\",\"READ_APP_STATUS\",\"READ_CURR" \
    "ENT_CHANNEL\",\"READ_INPUT_DEVICE_LIST\",\"READ_NETWORK_STATE\",\"READ_RUNNING_APPS\",\"READ_" \
    "TV_CHANNEL_LIST\",\"WRITE_NOTIFICATION_TOAST\",\"READ_POWER_STATE\",\"READ_COUNTRY_INFO\",\"C" \
    "ONTROL_INPUT_TEXT\",\"CONTROL_MOUSE_AND_KEYBOARD\",\"READ_INSTALLED_APPS\",\"READ_SETTINGS\"]" \
    ",\"signatures\":[{\"signature\":\"eyJhbGdvcml0aG0iOiJSU0EtU0hBMjU2Iiwia2V5SWQiOiJ0ZXN0LXNpZ25" \
    "pbmctY2VydCIsInNpZ25hdHVyZVZlcnNpb24iOjF9.hrVRgjCwXVvE2OOSpDZ58hR+59aFNwYDyjQgKk3auukd7pcegmE" \
    "2CzPCa0bJ0ZsRAcKkCTJrWo5iDzNhMBWRyaMOv5zWSrthlf7G128qvIlpMT0YNY+n/FaOHE73uLrS/g7swl3/qH/BGFG2" \
    "Hu4RlL48eb3lLKqTt2xKHdCs6Cd4RMfJPYnzgvI4BNrFUKsjkcu+WD4OO2A27Pq1n50cMchmcaXadJhGrOqH5YmHdOCj5" \
    "NSHzJYrsW0HPlpuAx/ECMeIZYDh6RMqaFM2DXzdKX9NmmyqzJ3o/0lkk/N97gfVRLW5hA29yeAwaCViZNCP8iC9aO0q9f" \
    "Qojoa7NQnAtw==\",\"signatureVersion\":1}],\"signed\":{\"appId\":\"com.lge.test\",\"created\":" \
    "\"20140509\",\"localizedAppNames\":{\"\":\"LG Remote App\",\"ko-KR\":\"\\ub9ac\\ubaa8\\ucee8 " \
    "\\uc571\",\"zxx-XX\":\"\\u041b\\u0413 R\\u044d\\u043cot\\u044d A\\u041f\\u041f\"},\"localized" \
    "VendorNames\":{\"\":\"LG Electronics\"},\"permissions\":[\"TEST_SECURE\",\"CONTROL_INPUT_TEXT" \
    "\",\"CONTROL_MOUSE_AND_KEYBOARD\",\"READ_INSTALLED_APPS\",\"READ_LGE_SDX\",\"READ_NOTIFICATIO" \
    "NS\",\"SEARCH\",\"WRITE_SETTINGS\",\"WRITE_NOTIFICATION_ALERT\",\"CONTROL_POWER\",\"READ_CURR" \
    "ENT_CHANNEL\",\"READ_RUNNING_APPS\",\"READ_UPDATE_INFO\",\"UPDATE_FROM_REMOTE_APP\",\"READ_LG" \
    "E_TV_INPUT_EVENTS\",\"READ_TV_CURRENT_TIME\"],\"serial\":\"2f930e2d2cfe083771f68e4fe7bb07\",\"" \
    "vendorId\":\"com.lge\"}}"

#define LG_MANIFEST_UNSIGNED \
    "{\"appVersion\":\"1.1\",\"manifestVersion\":1,\"permissions\":[\"APP_TO_APP\",\"CLOSE\",\"CON" \
    "TROL_AUDIO\",\"CONTROL_DISPLAY\",\"CONTROL_INPUT_JOYSTICK\",\"CONTROL_INPUT_MEDIA_PLAYBACK\"," \
    "\"CONTROL_INPUT_MEDIA_RECORDING\",\"CONTROL_INPUT_TEXT\",\"CONTROL_INPUT_TV\",\"CONTROL_MOUSE" \
    "_AND_KEYBOARD\",\"CONTROL_POWER\",\"CONTROL_TV_SCREEN\",\"LAUNCH\",\"LAUNCH_WEBAPP\",\"READ_A" \
    "PP_STATUS\",\"READ_COUNTRY_INFO\",\"READ_CURRENT_CHANNEL\",\"READ_INPUT_DEVICE_LIST\",\"READ_" \
    "INSTALLED_APPS\",\"READ_LGE_SDX\",\"READ_LGE_TV_INPUT_EVENTS\",\"READ_NETWORK_STATE\",\"READ_" \
    "NOTIFICATIONS\",\"READ_POWER_STATE\",\"READ_RUNNING_APPS\",\"READ_SETTINGS\",\"READ_TV_CHANNE" \
    "L_LIST\",\"READ_TV_CURRENT_TIME\",\"READ_UPDATE_INFO\",\"SEARCH\",\"TEST_OPEN\",\"TEST_PROTEC" \
    "TED\",\"TEST_SECURE\",\"UPDATE_FROM_REMOTE_APP\",\"WRITE_NOTIFICATION_ALERT\",\"WRITE_NOTIFIC" \
    "ATION_TOAST\",\"WRITE_SETTINGS\"]}"

#endif
