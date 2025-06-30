import sys
import json
import logging
import traceback
from pathlib import Path


# --- ОСНОВНАЯ ЛОГИКА ---
def main():
    try:
        
        if len(sys.argv) < 2:
            raise ValueError("No input .ass file was provided.")

        ass_files_to_check = [Path(p) for p in sys.argv[1:]]

        # --- Импорт библиотеки (включая FontLoader) ---
        from font_collector import (
            AssDocument,
            FontCollection,
            FontLoader,  # <--- Ключевой импорт!
            FontSelectionStrategyLibass,
            set_loglevel
        )
        set_loglevel(logging.CRITICAL)

        # --- *** НОВЫЙ, ПРАВИЛЬНЫЙ СПОСОБ ЗАГРУЗКИ ШРИФТОВ *** ---
        additional_fonts_loaded = []
        parent_dir = ass_files_to_check[0].parent
        attached_fonts_path = parent_dir / "attached_fonts"
        
        if attached_fonts_path.is_dir():
            # Используем FontLoader, как в тестах разработчиков
            additional_fonts_loaded = FontLoader.load_additional_fonts([attached_fonts_path])

        # --- Инициализация библиотеки с результатом от FontLoader ---
        font_collection = FontCollection(
            use_system_font=True, 
            additional_fonts=additional_fonts_loaded  # <--- Передаем сюда результат
        )
        
        # --- Анализ .ass файла ---
        font_strategy = FontSelectionStrategyLibass()
        found_fonts = set()
        not_found_names = set()

        subtitle = AssDocument.from_file(ass_files_to_check[0])
        used_styles = subtitle.get_used_style(collect_draw_fonts=True)

        for style, _ in used_styles.items():
            font_result = font_collection.get_used_font_by_style(style, font_strategy)
            if font_result and font_result.font_face:
                found_fonts.add(font_result.font_face.font_file)
            else:
                not_found_names.add(style.fontname)
        
        # --- Формирование результата ---
        result = {
            "found_fonts": [],
            "not_found_font_names": sorted(list(not_found_names))
        }
        for font_file in found_fonts:
            result["found_fonts"].append({
                "path": str(font_file.filename.resolve()),
                "family_name": font_file.font_faces[0].get_best_family_name().value
            })
        
        # Для отладки посмотрим, какие шрифты так и не нашлись, и какие внутренние имена у тех, что нашлись
        for font_file in found_fonts:
            names = {face.get_best_family_name().value for face in font_file.font_faces}

        print(json.dumps(result, indent=2, ensure_ascii=False))
        
    except Exception as e:
        print(json.dumps(error_info, indent=2), file=sys.stderr)
        sys.exit(1)

# --- Точка входа ---
if __name__ == "__main__":
    main()