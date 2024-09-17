idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES firebase_cert.pem
                       REQUIRES esp_wifi esp_http_client nvs_flash esp_event ) 
