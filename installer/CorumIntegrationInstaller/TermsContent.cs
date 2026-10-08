namespace CorumIntegrationInstaller;

internal static class TermsContent
{
    public const string Text = """
        Corum Integration 이용 및 기록 검증 데이터 약관

        Corum Integration은 Corum 맵 정보 표시와 기록 제출을 위한 Geometry Dash Geode 모드입니다.
        설치하거나 사용하기 전에 아래 내용을 확인해 주세요.

        1. 처리하는 정보

        기록 제출 기능이 활성화된 경우 다음 정보를 처리할 수 있습니다.

        • 현재 로그인된 Geometry Dash 계정 ID와 닉네임
        • 맵 코드, 맵 제목, 최고 기록, 시도 횟수와 점프 횟수
        • 플랫폼, Geometry Dash 버전, Geode 버전과 Corum Integration 버전
        • 게임 시작 시 로드된 비내장 Geode 모드의 ID와 버전 목록
        • 등록된 Corum 맵을 Normal Mode의 비 Test Mode에서 100% 완료했을 때의 Geometry Dash End Level 화면 PNG
        • PNG에 함께 표시되는 Geometry Dash 닉네임과 맵 제목

        2. 생성 및 전송 시점

        • End Level PNG는 기록 제출 기능이 켜진 상태에서 조건에 맞는 Corum 맵을 완료했을 때 게임 내부 장면만으로 생성됩니다.
        • PNG는 먼저 해당 Geode 모드의 전용 로컬 저장 공간에 보관됩니다.
        • 클리어 순간에는 PNG를 서버로 전송하지 않습니다.
        • 사용자가 종이비행기 창의 Submit 또는 메인 메뉴의 Submit All을 누른 경우에만 기록 데이터와 사용 가능한 증거가 서버로 전송됩니다.
        • 로컬 PNG가 없거나 사용할 수 없는 경우에는 증거 없이 기록만 제출될 수 있습니다.

        3. 범위와 보관

        • 다른 앱, 바탕화면, 알림창 또는 운영체제 UI는 캡처하지 않습니다.
        • 기록과 증거 연결이 성공하면 해당 pending PNG는 로컬 저장 공간에서 삭제됩니다.
        • 서버에 전송된 PNG와 기록 메타데이터는 비공개 기록 검증 자료로 보관되며 운영자가 관리합니다.
        • 모드 삭제나 로컬 데이터 삭제는 이미 서버에 제출된 기록 또는 증거를 자동으로 삭제하지 않습니다.

        4. 선택 및 동의

        • Geode 설정에서 Record submission을 끄면 기록 제출 UI와 새로운 End Level 검증 이미지 준비가 비활성화됩니다.
        • 이 데이터 처리에 동의하지 않는 경우 기록 제출 기능을 끄거나 모드를 설치하지 마세요.
        • 제출된 자료에 관한 문의는 모드를 배포한 Corum 디스코드 서버 관리자에게 해 주세요.

        설치기에서 ‘동의합니다.’를 선택하면 위 내용을 확인하고 동의한 것으로 처리됩니다.
        ‘동의하지 않습니다.’를 선택하면 설치 또는 업데이트를 진행할 수 없습니다.
        """;
}
