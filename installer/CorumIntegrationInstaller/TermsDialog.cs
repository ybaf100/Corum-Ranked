namespace CorumIntegrationInstaller;

internal sealed class TermsDialog : Form
{
    public TermsDialog()
    {
        Text = "Corum Integration 약관";
        StartPosition = FormStartPosition.CenterParent;
        MinimumSize = new Size(620, 460);
        Size = new Size(760, 620);
        ShowIcon = false;
        MaximizeBox = true;
        MinimizeBox = false;
        AutoScaleMode = AutoScaleMode.Dpi;
        Font = new Font("Segoe UI", 9F);

        var textBox = new RichTextBox
        {
            Dock = DockStyle.Fill,
            ReadOnly = true,
            DetectUrls = false,
            WordWrap = true,
            ScrollBars = RichTextBoxScrollBars.Vertical,
            BackColor = SystemColors.Window,
            BorderStyle = BorderStyle.FixedSingle,
            Text = TermsContent.Text
        };

        var closeButton = new Button
        {
            Text = "닫기",
            AutoSize = true,
            DialogResult = DialogResult.OK,
            Padding = new Padding(18, 4, 18, 4),
            Anchor = AnchorStyles.Right
        };

        var buttonPanel = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            Height = 52,
            FlowDirection = FlowDirection.RightToLeft,
            Padding = new Padding(10)
        };
        buttonPanel.Controls.Add(closeButton);

        Controls.Add(textBox);
        Controls.Add(buttonPanel);
        AcceptButton = closeButton;
        CancelButton = closeButton;
    }
}
