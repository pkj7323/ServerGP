import os
import glob
try:
    from PIL import Image
except ImportError:
    print("Pillow 라이브러리가 설치되어 있지 않습니다. 'pip install Pillow' 명령어를 실행해주세요.")
    exit(1)

def convert_png_to_bmp_with_colorkey(directory="."):
    """
    현재 디렉토리의 모든 PNG 파일을 찾아서,
    투명한 배경을 마젠타(RGB 255, 0, 255)로 채운 후 BMP로 저장합니다.
    """
    # 마젠타 색상 (Color Key로 자주 사용됨)
    MAGENTA = (255, 0, 255)

    png_files = glob.glob(os.path.join(directory, "*.png"))
    
    if not png_files:
        print("변환할 PNG 파일을 찾을 수 없습니다.")
        return

    print(f"총 {len(png_files)}개의 PNG 파일을 발견했습니다. 변환을 시작합니다...")

    success_count = 0
    for png_path in png_files:
        try:
            # 1. 원본 PNG 열기
            with Image.open(png_path) as img:
                # RGBA(알파 채널 포함) 형식으로 강제 변환
                img = img.convert("RGBA")
                
                # 2. 마젠타 배경의 새로운 이미지 생성 (원본과 동일한 크기)
                background = Image.new("RGB", img.size, MAGENTA)
                
                # 3. 투명도(Alpha)를 기준으로 마젠타 배경 위에 PNG를 합성
                # (알파값이 0인 부분은 배경인 마젠타가 그대로 드러나게 됩니다)
                background.paste(img, mask=img.split()[3]) 
                
                # 4. 저장할 BMP 경로 설정 (확장자만 .bmp로 변경)
                bmp_path = os.path.splitext(png_path)[0] + ".bmp"
                
                # 5. BMP로 저장
                background.save(bmp_path, format="BMP")
                success_count += 1
                print(f"[완료] {os.path.basename(png_path)} -> {os.path.basename(bmp_path)}")
                
        except Exception as e:
            print(f"[실패] {os.path.basename(png_path)} 변환 중 오류 발생: {e}")

    print(f"\n작업 완료! (총 {len(png_files)}개 중 {success_count}개 변환 성공)")
    print("※ 원본 .png 파일들은 그대로 유지되었습니다.")

if __name__ == "__main__":
    # 스크립트가 실행된 현재 디렉토리를 기준으로 실행
    convert_png_to_bmp_with_colorkey()
