using System.Diagnostics;
using CorumIntegrationInstaller.Core;

namespace CorumIntegrationInstaller;

internal sealed class MainForm : Form
{
    private static readonly Color SuccessColor = Color.FromArgb(30, 126, 71);
    private static readonly Color ErrorColor = Color.FromArgb(181, 45, 45);
    private static readonly Color MutedColor = Color.FromArgb(92, 101, 112);

    private readonly GeometryDashLocator _geometryDashLocator;
    private readonly SettingsStore _settingsStore;
    private readonly GeodePackageInspector _packageInspector;
    private readonly GitHubReleaseClient _releaseClient;
    private readonly InstallationService _installationService;

    private readonly Label _geometryDashStatus = CreateValueLabel();
    private readonly Label _geodeStatus = CreateValueLabel();
    private readonly Label _installedStatus = CreateValueLabel();
    private readonly Label _latestStatus = CreateValueLabel();
    private readonly Label _statusMessage = new();
    private readonly Button _browseButton = new();
    private readonly Button _geodeButton = new();
    private readonly Button _installButton = new();
    private readonly Button _refreshButton = new();
    private readonly Button _exitButton = new();
    private readonly Button _readTermsButton = new();
    private readonly CheckBox _agreeToTerms = new();
    private readonly CheckBox _disagreeWithTerms = new();
    private readonly Button _nextButton = new();
    private readonly Button _termsExitButton = new();
    private readonly ProgressBar _progressBar = new();

    private Control? _termsPage;
    private Control? _installerPage;
    private string? _geometryDashDirectory;
    private bool _geodeInstalled;
    private bool _busy;
    private bool _replacementInProgress;
    private bool _termsAccepted;
    private ReleaseInfo? _latestRelease;
    private IReadOnlyList<GeodePackageMetadata> _installedPackages = Array.Empty<GeodePackageMetadata>();

    public MainForm(
        GeometryDashLocator geometryDashLocator,
        SettingsStore settingsStore,
        GeodePackageInspector packageInspector,
        GitHubReleaseClient releaseClient,
        InstallationService installationService)
    {
        _geometryDashLocator = geometryDashLocator;
        _settingsStore = settingsStore;
        _packageInspector = packageInspector;
        _releaseClient = releaseClient;
        _installationService = installationService;

        InitializeWindow();
        BuildLayout();
        WireEvents();
    }

    protected override void OnShown(EventArgs eventArgs)
    {
        base.OnShown(eventArgs);
        ShowTermsPage();
    }

    protected override void OnFormClosing(FormClosingEventArgs eventArgs)
    {
        if (_replacementInProgress && eventArgs.CloseReason == CloseReason.UserClosing)
        {
            eventArgs.Cancel = true;
            MessageBox.Show(
                this,
                "Please wait for the current check or installation step to finish.",
                "Corum Integration Installer",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        base.OnFormClosing(eventArgs);
    }

    private void InitializeWindow()
    {
        Text = "Corum Integration Installer";
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        MinimizeBox = true;
        ShowIcon = false;
        ClientSize = new Size(680, 520);
        MinimumSize = new Size(696, 559);
        MaximumSize = MinimumSize;
        AutoScaleMode = AutoScaleMode.Dpi;
        Font = new Font("Segoe UI", 9F);
        BackColor = Color.FromArgb(248, 249, 251);
    }

    private void BuildLayout()
    {
        var installerPage = CreateInstallerPage();
        var termsPage = CreateTermsPage();
        _installerPage = installerPage;
        _termsPage = termsPage;
        installerPage.Visible = false;

        Controls.Add(installerPage);
        Controls.Add(termsPage);
    }

    private Control CreateTermsPage()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(28, 24, 28, 20),
            ColumnCount = 1,
            RowCount = 6,
            BackColor = BackColor
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 66));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 62));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 226));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 58));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        root.Controls.Add(CreateHeader(), 0, 0);
        root.Controls.Add(new Label
        {
            Text = "설치 또는 업데이트를 계속하기 전에 약관을 확인하고 동의 여부를 선택해 주세요.",
            Dock = DockStyle.Fill,
            ForeColor = MutedColor,
            TextAlign = ContentAlignment.MiddleLeft,
            Padding = new Padding(2, 0, 2, 0)
        }, 0, 1);
        root.Controls.Add(CreateSeparator(), 0, 2);
        root.Controls.Add(CreateTermsPanel(), 0, 3);
        root.Controls.Add(CreateTermsNavigationPanel(), 0, 4);

        return root;
    }

    private Control CreateInstallerPage()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(28, 24, 28, 20),
            ColumnCount = 1,
            RowCount = 7,
            BackColor = BackColor
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 66));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 202));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 58));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        root.Controls.Add(CreateHeader(), 0, 0);
        root.Controls.Add(CreateStatusPanel(), 0, 1);
        root.Controls.Add(CreateActionPanel(), 0, 2);
        root.Controls.Add(CreateSeparator(), 0, 3);

        _progressBar.Dock = DockStyle.Fill;
        _progressBar.Style = ProgressBarStyle.Marquee;
        _progressBar.MarqueeAnimationSpeed = 0;
        _progressBar.Visible = false;
        root.Controls.Add(_progressBar, 0, 4);

        _statusMessage.Dock = DockStyle.Fill;
        _statusMessage.ForeColor = MutedColor;
        _statusMessage.TextAlign = ContentAlignment.MiddleLeft;
        _statusMessage.AutoEllipsis = true;
        root.Controls.Add(_statusMessage, 0, 5);

        return root;
    }

    private Control CreateHeader()
    {
        var panel = new Panel { Dock = DockStyle.Fill };
        var title = new Label
        {
            Text = "Corum Integration",
            Font = new Font("Segoe UI Semibold", 20F, FontStyle.Bold),
            AutoSize = true,
            Location = new Point(0, 0),
            ForeColor = Color.FromArgb(30, 34, 40)
        };
        var subtitle = new Label
        {
            Text = $"{InstallerVersion.GetDisplayVersion()} Installer · Updater",
            AutoSize = true,
            Location = new Point(2, 42),
            ForeColor = MutedColor
        };
        panel.Controls.Add(title);
        panel.Controls.Add(subtitle);
        return panel;
    }

    private Control CreateStatusPanel()
    {
        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.White,
            Padding = new Padding(18, 12, 18, 12),
            ColumnCount = 3,
            RowCount = 4,
            CellBorderStyle = TableLayoutPanelCellBorderStyle.Single
        };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 154));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 104));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 43));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 43));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 43));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 43));

        _browseButton.Text = "Browse…";
        ConfigureSmallButton(_browseButton);
        _geodeButton.Text = "Get Geode";
        ConfigureSmallButton(_geodeButton);

        AddStatusRow(panel, 0, "Geometry Dash", _geometryDashStatus, _browseButton);
        AddStatusRow(panel, 1, "Geode", _geodeStatus, _geodeButton);
        AddStatusRow(panel, 2, "Corum Integration", _installedStatus, null);
        AddStatusRow(panel, 3, "Latest Release", _latestStatus, null);
        return panel;
    }

    private Control CreateActionPanel()
    {
        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(0, 10, 0, 2),
            ColumnCount = 3,
            RowCount = 1
        };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));

        _installButton.Text = "Install";
        _installButton.Dock = DockStyle.Fill;
        _installButton.Font = new Font("Segoe UI Semibold", 9.5F, FontStyle.Bold);

        _refreshButton.Text = "Refresh";
        _refreshButton.Dock = DockStyle.Fill;
        _refreshButton.Margin = new Padding(8, 0, 0, 0);

        _exitButton.Text = "Exit";
        _exitButton.Dock = DockStyle.Fill;
        _exitButton.Margin = new Padding(8, 0, 0, 0);

        panel.Controls.Add(_installButton, 0, 0);
        panel.Controls.Add(_refreshButton, 1, 0);
        panel.Controls.Add(_exitButton, 2, 0);
        return panel;
    }

    private static Control CreateSeparator() => new Panel
    {
        Dock = DockStyle.Top,
        Height = 1,
        Margin = new Padding(0, 12, 0, 0),
        BackColor = Color.FromArgb(218, 221, 226)
    };

    private Control CreateTermsPanel()
    {
        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 2,
            Padding = new Padding(0, 2, 0, 0),
            BackColor = BackColor
        };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 56));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 44));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var heading = new Label
        {
            Text = "약관 동의",
            Dock = DockStyle.Fill,
            Font = new Font("Segoe UI Semibold", 10.5F, FontStyle.Bold),
            ForeColor = Color.FromArgb(35, 39, 45),
            TextAlign = ContentAlignment.MiddleLeft
        };
        panel.SetColumnSpan(heading, 2);

        var termsPanel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            Margin = new Padding(30, 10, 28, 8)
        };
        termsPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        termsPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));

        var termsLabel = new Label
        {
            Text = "Corum Integration 모드 약관",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.BottomLeft,
            ForeColor = Color.FromArgb(35, 39, 45)
        };

        _readTermsButton.Text = "약관 읽기";
        _readTermsButton.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
        _readTermsButton.Size = new Size(138, 34);
        _readTermsButton.Margin = new Padding(0, 3, 0, 3);
        termsPanel.Controls.Add(termsLabel, 0, 0);
        termsPanel.Controls.Add(_readTermsButton, 0, 1);

        var choicePanel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            Margin = new Padding(18, 12, 0, 8)
        };
        choicePanel.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        choicePanel.RowStyles.Add(new RowStyle(SizeType.Percent, 50));

        ConfigureTermsChoice(_agreeToTerms, "동의합니다.");
        ConfigureTermsChoice(_disagreeWithTerms, "동의하지 않습니다.");
        choicePanel.Controls.Add(_agreeToTerms, 0, 0);
        choicePanel.Controls.Add(_disagreeWithTerms, 0, 1);

        panel.Controls.Add(heading, 0, 0);
        panel.Controls.Add(termsPanel, 0, 1);
        panel.Controls.Add(choicePanel, 1, 1);
        return panel;
    }

    private Control CreateTermsNavigationPanel()
    {
        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(0, 10, 0, 2),
            ColumnCount = 3,
            RowCount = 1
        };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 116));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 124));

        _termsExitButton.Text = "Exit";
        _termsExitButton.Dock = DockStyle.Fill;
        _termsExitButton.Margin = new Padding(0, 0, 8, 0);

        _nextButton.Text = "다음";
        _nextButton.Dock = DockStyle.Fill;
        _nextButton.Font = new Font("Segoe UI Semibold", 9.5F, FontStyle.Bold);

        panel.Controls.Add(_termsExitButton, 1, 0);
        panel.Controls.Add(_nextButton, 2, 0);
        return panel;
    }

    private void WireEvents()
    {
        _browseButton.Click += BrowseButtonOnClick;
        _geodeButton.Click += (_, _) => OpenExternalUrl(AppConstants.GeodeInstallUrl);
        _refreshButton.Click += async (_, _) => await RefreshStateAsync(refreshRelease: true);
        _installButton.Click += InstallButtonOnClick;
        _exitButton.Click += (_, _) => Close();
        _termsExitButton.Click += (_, _) => Close();
        _nextButton.Click += NextButtonOnClick;
        _readTermsButton.Click += (_, _) =>
        {
            using var dialog = new TermsDialog();
            dialog.ShowDialog(this);
        };
        _agreeToTerms.CheckedChanged += (_, _) =>
        {
            if (_agreeToTerms.Checked)
            {
                _disagreeWithTerms.Checked = false;
            }

            UpdateControls();
        };
        _disagreeWithTerms.CheckedChanged += (_, _) =>
        {
            if (_disagreeWithTerms.Checked)
            {
                _agreeToTerms.Checked = false;
            }

            UpdateControls();
        };
    }

    private void ShowTermsPage()
    {
        _termsAccepted = false;
        if (_termsPage is not null)
        {
            _termsPage.Visible = true;
            _termsPage.BringToFront();
        }

        if (_installerPage is not null)
        {
            _installerPage.Visible = false;
        }

        AcceptButton = _nextButton;
        UpdateControls();
    }

    private async void NextButtonOnClick(object? sender, EventArgs eventArgs)
    {
        if (!_agreeToTerms.Checked)
        {
            MessageBox.Show(
                this,
                "약관을 읽고 ‘동의합니다.’를 선택한 뒤 다음 단계로 진행해 주세요.",
                "약관 동의 필요",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        _termsAccepted = true;
        if (_termsPage is not null)
        {
            _termsPage.Visible = false;
        }

        if (_installerPage is not null)
        {
            _installerPage.Visible = true;
            _installerPage.BringToFront();
        }

        AcceptButton = null;
        await RefreshStateAsync(refreshRelease: true);
    }

    private async Task RefreshStateAsync(bool refreshRelease)
    {
        SetBusy(true, "Checking Geometry Dash, Geode and Corum Integration…");
        InstallerException? localError = null;
        InstallerException? releaseError = null;

        try
        {
            var preferredDirectory = _geometryDashDirectory ?? _settingsStore.Load().GeometryDashDirectory;
            var localTask = Task.Run(() => DetectLocalState(preferredDirectory));
            var releaseTask = refreshRelease || _latestRelease is null
                ? _releaseClient.GetLatestReleaseAsync()
                : null;

            LocalState localState;
            try
            {
                localState = await localTask;
            }
            catch (InstallerException exception)
            {
                localError = exception;
                localState = new LocalState(null, false, Array.Empty<GeodePackageMetadata>());
            }

            try
            {
                if (releaseTask is not null)
                {
                    _latestRelease = await releaseTask;
                }
            }
            catch (InstallerException exception)
            {
                releaseError = exception;
                _latestRelease = null;
            }

            _geometryDashDirectory = localState.GeometryDashDirectory;
            _geodeInstalled = localState.GeodeInstalled;
            _installedPackages = localState.InstalledPackages;
            UpdateStatusLabels();

            if (localError is not null)
            {
                ShowStatus(localError.Message, isError: true);
            }
            else if (_geometryDashDirectory is null)
            {
                ShowStatus("Geometry Dash was not found. Choose its installation folder.", isError: true);
            }
            else if (!_geodeInstalled)
            {
                ShowStatus("Geode is not installed. Install Geode before installing Corum Integration.", isError: true);
            }
            else if (releaseError is not null)
            {
                ShowStatus(releaseError.Message, isError: true);
            }
            else
            {
                ShowStatus("Ready.", isError: false);
            }
        }
        catch (Exception)
        {
            ShowStatus("The installer status could not be refreshed. Check the selected folder and network connection, then try again.", isError: true);
            UpdateStatusLabels();
        }
        finally
        {
            SetBusy(false);
        }
    }

    private LocalState DetectLocalState(string? preferredDirectory)
    {
        var geometryDashDirectory = _geometryDashLocator.FindInstalledDirectory(preferredDirectory);
        if (geometryDashDirectory is null)
        {
            return new LocalState(null, false, Array.Empty<GeodePackageMetadata>());
        }

        var modsDirectory = Path.Combine(geometryDashDirectory, "geode", "mods");
        var geodeInstalled = Directory.Exists(modsDirectory);
        var installedPackages = geodeInstalled
            ? _packageInspector.FindInstalledCorumPackages(modsDirectory)
            : Array.Empty<GeodePackageMetadata>();
        return new LocalState(geometryDashDirectory, geodeInstalled, installedPackages);
    }

    private async void BrowseButtonOnClick(object? sender, EventArgs eventArgs)
    {
        using var dialog = new FolderBrowserDialog
        {
            Description = "Select the Geometry Dash installation folder containing GeometryDash.exe.",
            UseDescriptionForTitle = true,
            ShowNewFolderButton = false,
            InitialDirectory = _geometryDashDirectory ?? string.Empty
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        if (!GeometryDashLocator.IsValidGeometryDashDirectory(dialog.SelectedPath))
        {
            ShowInstallerError(new InstallerException(
                InstallerErrorCode.InvalidGeometryDashFolder,
                "The selected folder is not a valid Geometry Dash folder because GeometryDash.exe was not found."));
            return;
        }

        _geometryDashDirectory = Path.GetFullPath(dialog.SelectedPath);
        try
        {
            _settingsStore.Save(new InstallerSettings(_geometryDashDirectory));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            ShowStatus("Geometry Dash was found, but the selected path could not be saved for the next run.", isError: true);
        }

        await RefreshStateAsync(refreshRelease: _latestRelease is null);
    }

    private async void InstallButtonOnClick(object? sender, EventArgs eventArgs)
    {
        if (!_termsAccepted)
        {
            MessageBox.Show(
                this,
                "약관 동의 단계를 완료한 뒤 설치 또는 업데이트를 진행해 주세요.",
                "약관 동의 필요",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        var geometryDashDirectory = _geometryDashDirectory;
        var latestRelease = _latestRelease;
        if (geometryDashDirectory is null || !_geodeInstalled || latestRelease is null)
        {
            ShowStatus("Installation prerequisites are not ready. Refresh the status and try again.", isError: true);
            return;
        }

        TemporaryDownload? download = null;
        SetBusy(true, $"Downloading {latestRelease.Asset.Name}…");
        try
        {
            var progress = new Progress<DownloadProgress>(UpdateDownloadProgress);
            download = await _releaseClient.DownloadPackageAsync(latestRelease, progress);
            var downloadedFilePath = download.FilePath;

            SetMarqueeProgress("Validating the downloaded Geode package…");
            await Task.Run(() => _packageInspector.ValidateCorumPackage(downloadedFilePath, latestRelease.Version));

            SetMarqueeProgress("Installing Corum Integration safely…");
            InstallationResult result;
            _replacementInProgress = true;
            try
            {
                result = await Task.Run(() => _installationService.InstallOrUpdate(
                    downloadedFilePath,
                    geometryDashDirectory,
                    latestRelease.Version));
            }
            finally
            {
                _replacementInProgress = false;
            }

            await RefreshStateAsync(refreshRelease: false);
            MessageBox.Show(
                this,
                $"Corum Integration {result.InstalledVersion} was installed successfully.",
                "Installation complete",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            ShowStatus("Installation complete.", isError: false);
        }
        catch (InstallerException exception)
        {
            ShowInstallerError(exception);
        }
        catch (Exception)
        {
            ShowInstallerError(new InstallerException(
                InstallerErrorCode.InstallFailed,
                "An unexpected error prevented Corum Integration from being installed. The existing installation was preserved whenever possible."));
        }
        finally
        {
            download?.Dispose();
            SetBusy(false);
        }
    }

    private void UpdateStatusLabels()
    {
        if (_geometryDashDirectory is null)
        {
            SetValue(_geometryDashStatus, "Not found", success: false);
        }
        else
        {
            SetValue(_geometryDashStatus, $"Found · {_geometryDashDirectory}", success: true);
        }

        SetValue(_geodeStatus, _geodeInstalled ? "Installed" : "Not installed", _geodeInstalled);

        var installedPackage = SelectHighestInstalledPackage();
        if (installedPackage is null)
        {
            SetValue(_installedStatus, "Not installed", success: false);
        }
        else
        {
            var duplicateSuffix = _installedPackages.Count > 1
                ? $" · {_installedPackages.Count} matching packages"
                : string.Empty;
            SetValue(_installedStatus, $"Installed: {installedPackage.Version}{duplicateSuffix}", success: true);
        }

        if (_latestRelease is null)
        {
            SetValue(_latestStatus, "Unavailable", success: false);
        }
        else
        {
            SetValue(_latestStatus, $"Latest: {_latestRelease.Version}", success: true);
        }

        UpdateControls();
    }

    private void UpdateControls()
    {
        var installedPackage = SelectHighestInstalledPackage();
        SemanticVersion? installedVersion = null;
        SemanticVersion? latestVersion = null;

        if (installedPackage is not null &&
            SemanticVersion.TryParse(installedPackage.Version, out var parsedInstalledVersion))
        {
            installedVersion = parsedInstalledVersion;
        }

        if (_latestRelease is not null &&
            SemanticVersion.TryParse(_latestRelease.Version, out var parsedLatestVersion))
        {
            latestVersion = parsedLatestVersion;
        }

        var newerThanRelease = installedVersion is not null &&
                               latestVersion is not null &&
                               installedVersion > latestVersion;

        if (installedPackage is null)
        {
            _installButton.Text = "Install";
        }
        else if (installedVersion is not null &&
                 latestVersion is not null &&
                 installedVersion < latestVersion)
        {
            _installButton.Text = "Update";
        }
        else if (newerThanRelease)
        {
            _installButton.Text = "Newer version installed";
        }
        else
        {
            _installButton.Text = "Reinstall";
        }

        _installButton.Enabled = !_busy &&
                                 !newerThanRelease &&
                                 _termsAccepted &&
                                 _geometryDashDirectory is not null &&
                                 _geodeInstalled &&
                                 _latestRelease is not null;
        _browseButton.Enabled = !_busy;
        _geodeButton.Enabled = !_busy;
        _refreshButton.Enabled = !_busy;
        _exitButton.Enabled = !_busy;
        _readTermsButton.Enabled = !_busy;
        _agreeToTerms.Enabled = !_busy;
        _disagreeWithTerms.Enabled = !_busy;
        _nextButton.Enabled = !_busy && _agreeToTerms.Checked;
        _termsExitButton.Enabled = !_busy;
    }

    private GeodePackageMetadata? SelectHighestInstalledPackage()
    {
        GeodePackageMetadata? selected = null;
        SemanticVersion? selectedVersion = null;
        foreach (var package in _installedPackages)
        {
            if (!SemanticVersion.TryParse(package.Version, out var version))
            {
                selected ??= package;
                continue;
            }

            if (selectedVersion is null || version > selectedVersion)
            {
                selected = package;
                selectedVersion = version;
            }
        }

        return selected;
    }

    private void SetBusy(bool busy, string? message = null)
    {
        _busy = busy;
        _progressBar.Visible = busy;
        _progressBar.Style = ProgressBarStyle.Marquee;
        _progressBar.MarqueeAnimationSpeed = busy ? 24 : 0;
        if (message is not null)
        {
            ShowStatus(message, isError: false);
        }

        UseWaitCursor = busy;
        UpdateControls();
    }

    private void SetMarqueeProgress(string message)
    {
        _progressBar.Style = ProgressBarStyle.Marquee;
        _progressBar.MarqueeAnimationSpeed = 24;
        ShowStatus(message, isError: false);
    }

    private void UpdateDownloadProgress(DownloadProgress progress)
    {
        if (progress.TotalBytes is > 0)
        {
            _progressBar.Style = ProgressBarStyle.Continuous;
            _progressBar.MarqueeAnimationSpeed = 0;
            _progressBar.Minimum = 0;
            _progressBar.Maximum = 100;
            _progressBar.Value = Math.Clamp((int)(progress.BytesReceived * 100 / progress.TotalBytes.Value), 0, 100);
            ShowStatus($"Downloading… {_progressBar.Value}%", isError: false);
        }
        else
        {
            SetMarqueeProgress($"Downloading… {progress.BytesReceived / 1024:N0} KB");
        }
    }

    private void ShowInstallerError(InstallerException exception)
    {
        ShowStatus(exception.Message, isError: true);
        MessageBox.Show(
            this,
            exception.Message,
            GetErrorTitle(exception.Code),
            MessageBoxButtons.OK,
            MessageBoxIcon.Error);
    }

    private void ShowStatus(string message, bool isError)
    {
        _statusMessage.Text = message;
        _statusMessage.ForeColor = isError ? ErrorColor : MutedColor;
    }

    private static void OpenExternalUrl(string url)
    {
        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch (Exception exception) when (exception is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            MessageBox.Show(
                $"Could not open the browser. Open this address manually:\n{url}",
                "Open link",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }
    }

    private static string GetErrorTitle(InstallerErrorCode code) => code switch
    {
        InstallerErrorCode.GeometryDashNotFound => "Geometry Dash not found",
        InstallerErrorCode.InvalidGeometryDashFolder => "Invalid Geometry Dash folder",
        InstallerErrorCode.GeodeNotInstalled => "Geode not installed",
        InstallerErrorCode.GitHubApiFailure => "GitHub connection failed",
        InstallerErrorCode.ReleaseNotFound => "Release not found",
        InstallerErrorCode.ReleaseAssetNotFound => "Release package not found",
        InstallerErrorCode.DownloadFailed => "Download failed",
        InstallerErrorCode.InvalidGeodeArchive => "Invalid Geode package",
        InstallerErrorCode.ModJsonMissing => "mod.json missing",
        InstallerErrorCode.InvalidModJson => "Invalid mod.json",
        InstallerErrorCode.ModIdMismatch => "Wrong Mod ID",
        InstallerErrorCode.ModVersionMismatch => "Version mismatch",
        InstallerErrorCode.GeometryDashRunning => "Geometry Dash is running",
        InstallerErrorCode.FileCollision => "File collision",
        InstallerErrorCode.AccessDenied => "File access denied",
        _ => "Installation failed"
    };

    private static Label CreateValueLabel() => new()
    {
        Dock = DockStyle.Fill,
        AutoEllipsis = true,
        TextAlign = ContentAlignment.MiddleLeft,
        ForeColor = MutedColor,
        Padding = new Padding(8, 0, 8, 0)
    };

    private static void AddStatusRow(
        TableLayoutPanel panel,
        int row,
        string heading,
        Label value,
        Button? button)
    {
        var headingLabel = new Label
        {
            Text = heading,
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleLeft,
            Font = new Font("Segoe UI Semibold", 9F, FontStyle.Bold),
            Padding = new Padding(8, 0, 8, 0)
        };
        panel.Controls.Add(headingLabel, 0, row);
        panel.Controls.Add(value, 1, row);

        if (button is null)
        {
            var spacer = new Panel { Dock = DockStyle.Fill };
            panel.Controls.Add(spacer, 2, row);
        }
        else
        {
            panel.Controls.Add(button, 2, row);
        }
    }

    private static void ConfigureSmallButton(Button button)
    {
        button.Dock = DockStyle.Fill;
        button.Margin = new Padding(8, 6, 8, 6);
    }

    private static void ConfigureTermsChoice(CheckBox checkBox, string text)
    {
        checkBox.Text = text;
        checkBox.Dock = DockStyle.Fill;
        checkBox.AutoSize = false;
        checkBox.CheckAlign = ContentAlignment.MiddleLeft;
        checkBox.TextAlign = ContentAlignment.MiddleLeft;
        checkBox.Padding = new Padding(2, 0, 0, 0);
    }

    private static void SetValue(Label label, string value, bool success)
    {
        label.Text = value;
        label.ForeColor = success ? SuccessColor : ErrorColor;
    }

    private sealed record LocalState(
        string? GeometryDashDirectory,
        bool GeodeInstalled,
        IReadOnlyList<GeodePackageMetadata> InstalledPackages);
}
