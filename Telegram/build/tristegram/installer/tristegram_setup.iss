; Tristegram installer: language, a ready look to start with, shortcuts.
; Built in CI with: ISCC.exe /DSourceDir=<folder with Tristegram.exe>
;   /DMyAppVersion=<version> /DOutputDir=<folder> tristegram_setup.iss

#define MyAppName "Tristegram"
#define MyAppPublisher "Tristegram"
#define MyAppExeName "Tristegram.exe"
#define MyAppId "9F1C6B2E-3A47-4D8B-9E21-7C5A0B4D2F11"

#ifndef MyAppVersion
  #define MyAppVersion "1.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={userappdata}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=Tristegram-setup-x64
SetupIconFile={#SourcePath}..\..\..\Resources\art\icon256.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
WizardImageFile={#SourcePath}wizard_164.bmp,{#SourcePath}wizard_328.bmp
WizardSmallImageFile={#SourcePath}small_55.bmp,{#SourcePath}small_110.bmp
WizardImageStretch=yes
ShowLanguageDialog=yes
UsePreviousLanguage=no
CloseApplications=force
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[CustomMessages]
en.LookTitle=Choose your look
en.LookSubtitle=Tristegram starts with this theme. Everything can be changed later in the settings.
en.LookCaption=Ready themes:
en.LookTristegram=Tristegram: dark with orange
en.LookEmbers=Embers: dark with red
en.LookOcean=Ocean: dark with blue
en.LookSakura=Sakura: light with pink
en.LookNeon=Neon: dark with violet
en.LookForest=Forest: dark green
en.LookGraphite=Graphite: dark gray
en.LookKeep=Keep my current look
en.Autostart=Start Tristegram with Windows
en.Launch=Open Tristegram

ru.LookTitle=Выберите оформление
ru.LookSubtitle=Тристеграм запустится с этой темой. Всё можно поменять потом в настройках.
ru.LookCaption=Готовые темы:
ru.LookTristegram=Тристеграм: тёмная с оранжевым
ru.LookEmbers=Угли: тёмная с красным
ru.LookOcean=Океан: тёмная с синим
ru.LookSakura=Сакура: светлая с розовым
ru.LookNeon=Неон: тёмная с фиолетовым
ru.LookForest=Лес: тёмно-зелёная
ru.LookGraphite=Графит: тёмно-серая
ru.LookKeep=Оставить моё оформление
ru.Autostart=Запускать Тристеграм вместе с Windows
ru.Launch=Открыть Тристеграм

uk.LookTitle=Оберіть оформлення
uk.LookSubtitle=Тристеграм запуститься з цією темою. Усе можна змінити потім у налаштуваннях.
uk.LookCaption=Готові теми:
uk.LookTristegram=Тристеграм: темна з помаранчевим
uk.LookEmbers=Жар: темна з червоним
uk.LookOcean=Океан: темна з синім
uk.LookSakura=Сакура: світла з рожевим
uk.LookNeon=Неон: темна з фіолетовим
uk.LookForest=Ліс: темно-зелена
uk.LookGraphite=Графіт: темно-сіра
uk.LookKeep=Залишити моє оформлення
uk.Autostart=Запускати Тристеграм разом з Windows
uk.Launch=Відкрити Тристеграм

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "{cm:Autostart}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "-autostart"; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:Launch}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\tristegram_install.json"
Type: files; Name: "{app}\tristegram_install.applied"

[Code]
var
  LookPage: TInputOptionWizardPage;

procedure InitializeWizard;
begin
  LookPage := CreateInputOptionPage(
    wpSelectTasks,
    CustomMessage('LookTitle'),
    CustomMessage('LookSubtitle'),
    CustomMessage('LookCaption'),
    True,
    False);
  LookPage.Add(CustomMessage('LookTristegram'));
  LookPage.Add(CustomMessage('LookEmbers'));
  LookPage.Add(CustomMessage('LookOcean'));
  LookPage.Add(CustomMessage('LookSakura'));
  LookPage.Add(CustomMessage('LookNeon'));
  LookPage.Add(CustomMessage('LookForest'));
  LookPage.Add(CustomMessage('LookGraphite'));
  LookPage.Add(CustomMessage('LookKeep'));
  LookPage.SelectedValueIndex := 0;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Theme: Integer;
  Json: String;
begin
  if CurStep = ssPostInstall then
  begin
    Theme := LookPage.SelectedValueIndex;
    if Theme = 7 then
      Theme := -1;
    Json := '{"theme": ' + IntToStr(Theme)
      + ', "language": "' + ActiveLanguage + '"}';
    SaveStringToFile(
      ExpandConstant('{app}\tristegram_install.json'),
      Json,
      False);
  end;
end;
