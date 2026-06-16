#-----------------------------------------------------------------------------
# elements_modal — 純粋 SDL + Elements + ThorVG ベース JSON モーダル UI ライブラリ
#
# SDL3 を使う任意のアプリから単体で利用可能。
# C++20 必須 (Elements の <elements.hpp> 経由)。
#
# 依存:
#   - SDL3::SDL3 (PUBLIC, 公開 API に SDL_Window* が現れるため)
#   - cycfi::elements (PRIVATE, 実装内部のみ)
#   - thorvg (PRIVATE, Init/Term の直接呼出のため)
#   - picojson (PRIVATE, JSON parse のため)
#-----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

project(elements_modal LANGUAGES CXX)

add_library(elements_modal STATIC
	src/modal.cpp
	src/json_layout.cpp
	src/runtime.cpp
	src/overlay_session.cpp
	src/navigator.cpp
)

target_include_directories(elements_modal
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(elements_modal PUBLIC cxx_std_20)

# 依存ライブラリ。 SDL3 ターゲットは parent project 側で用意済みを前提。
if (TARGET SDL3::SDL3)
	target_link_libraries(elements_modal PUBLIC SDL3::SDL3)
elseif (TARGET SDL3-shared)
	target_link_libraries(elements_modal PUBLIC SDL3-shared)
endif()

if (TARGET cycfi::elements)
	target_link_libraries(elements_modal PRIVATE cycfi::elements)
endif()

if (TARGET thorvg)
	target_link_libraries(elements_modal PRIVATE thorvg)
endif()

# picojson はヘッダオンリー (vcpkg 提供パス) を include path に追加。
# parent 側で PICOJSON_INCLUDE_DIRS が定義されていればそれを使う。
if (DEFINED PICOJSON_INCLUDE_DIRS AND PICOJSON_INCLUDE_DIRS)
	target_include_directories(elements_modal PRIVATE ${PICOJSON_INCLUDE_DIRS})
else()
	find_path(_em_picojson_dir "picojson/picojson.h")
	if (_em_picojson_dir)
		target_include_directories(elements_modal PRIVATE ${_em_picojson_dir})
	endif()
endif()

# MSVC: UTF-8 ソース指定
if (MSVC)
	target_compile_options(elements_modal PRIVATE /utf-8)
endif()

add_library(elements_modal::elements_modal ALIAS elements_modal)

# ----------------------------------------------------------------------------
# Examples / standalone デモ (デフォルト OFF)
# ----------------------------------------------------------------------------
option(ELEMENTS_MODAL_BUILD_EXAMPLES
	"Build elements_modal standalone examples" OFF)

if (ELEMENTS_MODAL_BUILD_EXAMPLES)
	# elements_modal が cycfi::elements / thorvg / SDL3 / picojson だけで完結
	# することを示す standalone デモ群:
	#   hello_modal       — 単一モーダル (run_modal)
	#   navigator_screens — 複数画面の遷移 (overlay_session + navigator)
	set(_em_examples
		elements_modal_hello_example=examples/hello_modal.cpp
		elements_modal_navigator_example=examples/navigator_screens.cpp
	)
	foreach (_em_pair IN LISTS _em_examples)
		string(REPLACE "=" ";" _em_kv "${_em_pair}")
		list(GET _em_kv 0 _em_target)
		list(GET _em_kv 1 _em_src)

		add_executable(${_em_target} ${_em_src})
		target_link_libraries(${_em_target} PRIVATE elements_modal::elements_modal)
		# example は #include <elements.hpp> で load_fonts_from_directory を直接
		# 呼ぶため、 elements ターゲットを (在れば) 例にも直接リンクする。
		# elements_modal 本体は cycfi::elements を PRIVATE 依存のまま保つ。
		if (TARGET cycfi::elements)
			target_link_libraries(${_em_target} PRIVATE cycfi::elements)
		endif()
		target_compile_features(${_em_target} PRIVATE cxx_std_20)
		if (MSVC)
			target_compile_options(${_em_target} PRIVATE /utf-8)
		endif()

		# Windows: 実行時に SDL3.dll が要るので exe の隣にコピー。
		# (SDL3 が shared lib target の場合のみ)
		if (TARGET SDL3-shared AND WIN32)
			add_custom_command(TARGET ${_em_target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					$<TARGET_FILE:SDL3-shared>
					$<TARGET_FILE_DIR:${_em_target}>
			)
		endif()
	endforeach()
endif()
