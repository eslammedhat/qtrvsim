#include "newdialog.h"
#include "mainwindow.h"
#include "qtmipsexception.h"

NewDialog::NewDialog(QWidget *parent, QSettings *settings) : QDialog(parent) {
    setWindowTitle("New machine");

    this->settings = settings;
    config = nullptr;

    ui = new Ui::NewDialog();
    ui->setupUi(this);
    ui_cache_p = new Ui::NewDialogCache();
    ui_cache_p->setupUi(ui->tab_cache_program);
    ui_cache_p->writeback_policy->hide();
    ui_cache_p->label_writeback->hide();
    ui_cache_d = new Ui::NewDialogCache();
    ui_cache_d->setupUi(ui->tab_cache_data);

    connect(ui->pushButton_load, SIGNAL(clicked(bool)), this, SLOT(create()));
    connect(ui->pushButton_cancel, SIGNAL(clicked(bool)), this, SLOT(cancel()));
    connect(ui->pushButton_browse, SIGNAL(clicked(bool)), this, SLOT(browse_elf()));
    connect(ui->preset_no_pipeline, SIGNAL(toggled(bool)), this, SLOT(set_preset()));
    connect(ui->preset_pipelined_bare, SIGNAL(toggled(bool)), this, SLOT(set_preset()));
    connect(ui->preset_pipelined_hazard, SIGNAL(toggled(bool)), this, SLOT(set_preset()));
    connect(ui->preset_pipelined, SIGNAL(toggled(bool)), this, SLOT(set_preset()));

    connect(ui->pipelined, SIGNAL(clicked(bool)), this, SLOT(pipelined_change(bool)));
    connect(ui->delay_slot, SIGNAL(clicked(bool)), this, SLOT(delay_slot_change(bool)));
    connect(ui->hazard_unit, SIGNAL(clicked(bool)), this, SLOT(hazard_unit_change()));
    connect(ui->hazard_stall, SIGNAL(clicked(bool)), this, SLOT(hazard_unit_change()));
    connect(ui->hazard_stall_forward, SIGNAL(clicked(bool)), this, SLOT(hazard_unit_change()));
    connect(ui->mem_protec_exec, SIGNAL(clicked(bool)), this, SLOT(mem_protec_exec_change(bool)));
    connect(ui->mem_protec_write, SIGNAL(clicked(bool)), this, SLOT(mem_protec_write_change(bool)));

	cache_handler_d = new NewDialogCacheHandler(this, ui_cache_d);
	cache_handler_p = new NewDialogCacheHandler(this, ui_cache_p);

    load_settings(); // Also configures gui
}

NewDialog::~NewDialog() {
    delete ui_cache_d;
    delete ui_cache_p;
    delete ui;
    // Settings is freed by parent
    delete config;
}

void NewDialog::switch2custom() {
	ui->preset_custom->setChecked(true);
	config_gui();
}

void NewDialog::closeEvent(QCloseEvent *) {
    load_settings(); // Reset from settings
    // Close main window if not already configured
    MainWindow *prnt = (MainWindow*)parent();
    if (!prnt->configured())
        prnt->close();
}

void NewDialog::cancel() {
    this->close();
}

void NewDialog::create() {
    MainWindow *prnt = (MainWindow*)parent();

    try {
        prnt->create_core(*config);
    } catch (const machine::QtMipsExceptionInput &e) {
        QMessageBox msg(this);
        msg.setText(e.msg(false));
        msg.setIcon(QMessageBox::Critical);
        msg.setToolTip("Please check that ELF executable really exists and is in correct format.");
        msg.setDetailedText(e.msg(true));
        msg.setWindowTitle("Error while initializing new machine");
        msg.exec();
        return;
    }

    store_settings(); // Save to settings
    this->close();
}

void NewDialog::browse_elf() {
    QFileDialog elf_dialog(this);
    elf_dialog.setFileMode(QFileDialog::ExistingFile);
    if (elf_dialog.exec()) {
        QString path = elf_dialog.selectedFiles()[0];
        ui->elf_file->setText(path);
        config->set_elf(path);
    }
    // Elf shouldn't have any other effect so we skip config_gui here
}

void NewDialog::set_preset() {
    unsigned pres_n = preset_number();
    if (pres_n > 0) {
        config->preset((enum machine::ConfigPresets)(pres_n - 1));
        config_gui();
    }
}

void NewDialog::pipelined_change(bool val) {
    config->set_pipelined(val);
	switch2custom();
}

void NewDialog::delay_slot_change(bool val) {
    config->set_delay_slot(val);
	switch2custom();
}

void NewDialog::hazard_unit_change() {
    if (ui->hazard_unit->isChecked()) {
        config->set_hazard_unit(ui->hazard_stall->isChecked() ? machine::MachineConfig::HU_STALL : machine::MachineConfig::HU_STALL_FORWARD);
	} else {
        config->set_hazard_unit(machine::MachineConfig::HU_NONE);
	}
	switch2custom();
}

void NewDialog::mem_protec_exec_change(bool v) {
    config->set_memory_execute_protection(v);
	switch2custom();
}

void NewDialog::mem_protec_write_change(bool v) {
    config->set_memory_write_protection(v);
	switch2custom();
}

void NewDialog::config_gui() {
    // Basic
    ui->elf_file->setText(config->elf());
    // Core
    ui->pipelined->setChecked(config->pipelined());
    ui->delay_slot->setChecked(config->delay_slot());
    ui->hazard_unit->setChecked(config->hazard_unit() != machine::MachineConfig::HU_NONE);
    ui->hazard_stall->setChecked(config->hazard_unit() == machine::MachineConfig::HU_STALL);
    ui->hazard_stall_forward->setChecked(config->hazard_unit() == machine::MachineConfig::HU_STALL_FORWARD);
    // Memory
    ui->mem_protec_exec->setChecked(config->memory_execute_protection());
    ui->mem_protec_write->setChecked(config->memory_write_protection());
    ui->mem_time_read->setValue(config->memory_access_time_read());
    ui->mem_time_write->setValue(config->memory_access_time_write());
    // Cache
	cache_handler_d->config_gui();
	cache_handler_p->config_gui();

    // Disable various sections according to configuration
    ui->delay_slot->setEnabled(!config->pipelined());
    ui->hazard_unit->setEnabled(config->pipelined());
}

unsigned NewDialog::preset_number() {
    enum machine::ConfigPresets preset;
    if (ui->preset_no_pipeline->isChecked())
        preset = machine::CP_SINGLE;
    else if (ui->preset_pipelined_bare->isChecked())
        preset = machine::CP_PIPE_NO_HAZARD;
    else if (ui->preset_pipelined_hazard->isChecked())
        preset = machine::CP_PIPE_NO_CACHE;
    else if (ui->preset_pipelined->isChecked())
        preset = machine::CP_PIPE_CACHE;
    else
        return 0;
    return (unsigned)preset + 1;
}

void NewDialog::load_settings() {
    if (config != nullptr)
        delete config;

    // Load config
    config = new machine::MachineConfig(settings);
	cache_handler_d->set_config(config->access_cache_data());
	cache_handler_p->set_config(config->access_cache_program());

    // Load preset
    unsigned preset = settings->value("Preset", 1).toUInt();
    if (preset != 0) {
        auto p = (enum machine::ConfigPresets)(preset - 1);
        config->preset(p);
        switch (p) {
        case machine::CP_SINGLE:
            ui->preset_no_pipeline->setChecked(true);
            break;
        case machine::CP_PIPE_NO_HAZARD:
            ui->preset_pipelined_bare->setChecked(true);
            break;
        case machine::CP_PIPE_NO_CACHE:
            ui->preset_pipelined_hazard->setChecked(true);
            break;
        case machine::CP_PIPE_CACHE:
            ui->preset_pipelined->setChecked(true);
            break;
        }
    } else {
        ui->preset_custom->setChecked(true);
	}

    config_gui();
}

void NewDialog::store_settings() {
    config->store(settings);

    // Presets are not stored in settings so we have to store them explicitly
    if (ui->preset_custom->isChecked()) {
        settings->setValue("Preset", 0);
	} else {
        settings->setValue("Preset", preset_number());
	}
}

NewDialogCacheHandler::NewDialogCacheHandler(NewDialog *nd, Ui::NewDialogCache *cui) {
	this->nd = nd;
	this->ui = cui;
	this->config = nullptr;
	connect(ui->enabled, SIGNAL(clicked(bool)), this, SLOT(enabled(bool)));
	connect(ui->number_of_sets, SIGNAL(editingFinished()), this, SLOT(numsets()));
	connect(ui->block_size, SIGNAL(editingFinished()), this, SLOT(blocksize()));
	connect(ui->degree_of_associativity, SIGNAL(editingFinished()), this, SLOT(degreeassociativity()));
	connect(ui->replacement_policy, SIGNAL(activated(int)), this, SLOT(replacement(int)));
	connect(ui->writeback_policy, SIGNAL(activated(int)), this, SLOT(writeback(int)));
}

void NewDialogCacheHandler::set_config(machine::MachineConfigCache *config) {
	this->config = config;
}

void NewDialogCacheHandler::config_gui() {
    ui->enabled->setChecked(config->enabled());
    ui->number_of_sets->setValue(config->sets());
    ui->block_size->setValue(config->blocks());
    ui->degree_of_associativity->setValue(config->associativity());
    ui->replacement_policy->setCurrentIndex((int)config->replacement_policy());
    ui->writeback_policy->setCurrentIndex((int)config->write_policy());
}

void NewDialogCacheHandler::enabled(bool val) {
	config->set_enabled(val);
	nd->switch2custom();
}

void NewDialogCacheHandler::numsets() {
	config->set_sets(ui->number_of_sets->value());
	nd->switch2custom();
}

void NewDialogCacheHandler::blocksize() {
	config->set_blocks(ui->block_size->value());
	nd->switch2custom();
}

void NewDialogCacheHandler::degreeassociativity() {
	config->set_associativity(ui->degree_of_associativity->value());
	nd->switch2custom();
}

void NewDialogCacheHandler::replacement(int val) {
	config->set_replacement_policy((enum machine::MachineConfigCache::ReplacementPolicy)val);
	nd->switch2custom();
}

void NewDialogCacheHandler::writeback(int val) {
	config->set_write_policy((enum machine::MachineConfigCache::WritePolicy)val);
	nd->switch2custom();
}
